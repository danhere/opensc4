// Our cSC4CrimeSimulator (build 641), second part: the monthly step and
// what it calls (docs/crime.md, "The step"). Each helper is called through
// the game's address, so each replacement can be switched on alone.
#include "core/crime_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>

namespace opensc4::crime_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("crime: first %s", name);
}

namespace {

using SelfFn = void(__thiscall*)(void* self);
using ExpectedFn = uint32_t(__thiscall*)(void* self);
using GenerateFn = uint32_t(__thiscall*)(void* self, int32_t count);
using IncidentFn = uint8_t(__thiscall*)(void* self, void* lot);
using MasksFn = uint8_t(__stdcall*)(void* lot, uint32_t* zone, uint32_t* building, uint32_t* size, uint32_t* wealth);
using AssignFn = void(__thiscall*)(void* buffer, const char* begin, const char* end, void* tag);
using CopyFn = void(__thiscall*)(void* buffer, const void* source, uint32_t position, uint32_t count, void* tag);

void* Random() { return reinterpret_cast<void*>(addr::cSC4CrimeSimulator_Random); }

// A grid's cells by row (grid slot 24 returns its vector of row vectors;
// each row is begin, end, capacity, so row x starts at [3 * x]).
template <typename T>
T* const* Rows(void* grid)
{
    return *Virtual<T* const* const*>(grid, 0x60);
}

// --- Helpers of the step ------------------------------------------------------

// 0x0069FB00 (state 1): the people of each of three groups (the service at
// 0x00B43DA4, slot 25: three uint16 grids) by the quarter of a second value
// (slot 23: three float grids, times 0.02) into +0x9C, four counts a group.
void __fastcall CountPeople(void* self, void*)
{
    static bool logged = false;
    FirstCall("CountPeople", logged);
    void* const service = Global<void*>(addr::SC4_Service43DA4);
    void* people[3];
    void* values[3];
    Virtual<uint32_t>(service, 0x64, &people[0], &people[1], &people[2]);
    Virtual<uint32_t>(service, 0x5C, &values[0], &values[1], &values[2]);
    const float scale = Global<float>(addr::SC4_Float0_02);
    for (int group = 0; group < 3; ++group) {
        auto* const counts = &At<uint32_t>(self, field::kCounts + 16 * group);
        counts[0] = counts[1] = counts[2] = counts[3] = 0;
        uint16_t* const* const peopleRows = Rows<uint16_t>(people[group]);
        float* const* const valueRows = Rows<float>(values[group]);
        const int32_t width = Virtual<int32_t>(people[group], 0x30);
        const int32_t height = Virtual<int32_t>(people[group], 0x34);
        for (int32_t x = 0; x < width; ++x)
            for (int32_t z = 0; z < height; ++z) {
                const int32_t quarter = Ftol(static_cast<double>(valueRows[3 * x][z]) * scale);
                At<uint32_t>(self, field::kCounts + 16 * group + 4 * quarter) += peopleRows[3 * x][z];
            }
    }
    for (int group = 0; group < 3; ++group) {
        Virtual<uint32_t>(people[group], 0x8);
        Virtual<uint32_t>(values[group], 0x8);
    }
}

// 0x0069F780 (state 1): for each group, from its demands (the table at
// 0x00B0D1F8, through the service at 0x00B43D74, slot 6): sums of slots 12,
// 14 and 15, then (12 - 15) / (14 - 15) * 2 - 1 into +0xCC.
void __fastcall ComputeFactors(void* self, void*)
{
    static bool logged = false;
    FirstCall("ComputeFactors", logged);
    void* const demands = Global<void*>(addr::SC4_Service43D74);
    const auto* const table = reinterpret_cast<const uint32_t*>(addr::CrimeTuning_DemandGroups);
    for (int group = 0; group < 3; ++group) {
        float value = 0.0f, high = 0.0f, low = 0.0f;
        for (const uint32_t* id = table + 6 * group; *id; ++id) {
            void* const demand = Virtual<void*>(demands, 0x18, *id, 0u);
            value = static_cast<float>(Virtual<double>(demand, 0x30) + value);
            high = static_cast<float>(Virtual<double>(demand, 0x38) + high);
            low = static_cast<float>(Virtual<double>(demand, 0x3C) + low);
        }
        const double ratio = (static_cast<double>(value) - low) / (static_cast<double>(high) - low);
        At<float>(self, field::kFactors + 4 * group) = static_cast<float>(ratio + ratio - Global<float>(addr::SC4_FloatOneA81228));
    }
}

// 0x0069F360 (state 2): the expected crimes: for each group, its counts by
// the quarter curves at 0, 25, 50 and 75 (float sums), plus the factor curve
// at the group's factor times its people.
uint32_t __fastcall ExpectedCrimes(void* self, void*)
{
    static bool logged = false;
    FirstCall("ExpectedCrimes", logged);
    uint32_t expected = 0;
    for (int group = 0; group < 3; ++group) {
        const auto* const counts = &At<uint32_t>(self, field::kCounts + 16 * group);
        const char* const quarterCurve = static_cast<char*>(self) + field::kQuarterCurves + 16 * group;
        float sum = 0.0f;
        uint32_t people = 0;
        for (int quarter = 0; quarter < 4; ++quarter) {
            const double y = EvaluateCurve(quarterCurve, static_cast<float>(quarter * 25));
            people += counts[quarter];
            sum = static_cast<float>(y * static_cast<int32_t>(counts[quarter]) + sum);
        }
        const char* const factorCurve = static_cast<char*>(self) + field::kFactorCurves + 16 * group;
        const double y = EvaluateCurve(factorCurve, At<float>(self, field::kFactors + 4 * group));
        expected += static_cast<uint32_t>(Ftol(y * static_cast<int32_t>(people) + sum));
    }
    return expected;
}

// 0x0069FC40 (state 4): every non-zero cell of the crime grid times the
// decay (+0x16C), truncated; the hotspot's value re-read.
void __fastcall Decay(void* self, void*)
{
    static bool logged = false;
    FirstCall("Decay", logged);
    void* const grid = At<void*>(self, field::kCrimeGrid);
    uint8_t* const* const rows = Rows<uint8_t>(grid);
    const int32_t width = Virtual<int32_t>(grid, 0x30);
    const int32_t height = Virtual<int32_t>(grid, 0x34);
    for (int32_t x = 0; x < width; ++x)
        for (int32_t z = 0; z < height; ++z) {
            uint8_t& cell = rows[3 * x][z];
            if (cell)
                cell = static_cast<uint8_t>(Ftol(static_cast<double>(static_cast<int32_t>(cell)) *
                                                 At<float>(self, field::kDecay)));
        }
    At<int32_t>(self, field::kHotspotValue) =
        rows[3 * At<int32_t>(self, field::kHotspotX)][At<int32_t>(self, field::kHotspotZ)];
}

// 0x006A1530 (state 4, Shutdown): the incidents freed (name, occupant's
// reference, the record), the vector emptied.
void __fastcall ClearIncidents(void* self, void*)
{
    static bool logged = false;
    FirstCall("ClearIncidents", logged);
    auto& incidents = At<Vector>(self, field::kIncidents);
    for (uint32_t i = 0; i < static_cast<uint32_t>((incidents.end - incidents.begin) >> 2); ++i) {
        auto* const incident = reinterpret_cast<Incident**>(incidents.begin)[i];
        if (!incident)
            continue;
        incident->nameVtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (incident->nameBegin)
            Free(incident->nameBegin);
        if (incident->occupant)
            Virtual<uint32_t>(incident->occupant, 0x8);
        OperatorDelete(incident);
    }
    incidents.end = incidents.begin;
}

// 0x0069FD70 (state 4): the average of the crime grid over zoned cells
// (zone manager 0x00B43D14, slot 12, at the cell times the grid's cell
// size), as 64-bit sums.
void __fastcall UpdateAverage(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateAverage", logged);
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    void* const grid = At<void*>(self, field::kCrimeGrid);
    uint8_t* const* const rows = Rows<uint8_t>(grid);
    const int32_t width = Virtual<int32_t>(grid, 0x30);
    const int32_t height = Virtual<int32_t>(grid, 0x34);
    const int32_t cellSize = Virtual<int32_t>(grid, 0x28);
    int64_t sum = 0, cells = 0;
    for (int32_t x = 0; x < width; ++x)
        for (int32_t z = 0; z < height; ++z)
            if (Virtual<int32_t>(zones, 0x30, x * cellSize, z * cellSize)) {
                sum += rows[3 * x][z];
                ++cells;
            }
    At<float>(self, field::kAverageCrime) = static_cast<float>(static_cast<double>(sum) / static_cast<double>(cells));
}

// --- Placing crimes -----------------------------------------------------------

// 0x0069F850: the four masks a lot's crime type must match: its zone's kind
// (zone manager slot 12 at the lot's cell: 1-3 -> 1, 4-6 -> 2, 7-9 -> 4),
// its building's two levels (slot 11: 1 << (level - 1), the first 0x100 when
// 0) and its wealth (lot slot 35: 0-1 -> 1, 2-3 and 5-6 -> 2, 4 -> 4);
// 0x80000000 where there is none.
uint8_t __stdcall LotMasks(void* lot, uint32_t* zone, uint32_t* building, uint32_t* size, uint32_t* wealth)
{
    static bool logged = false;
    FirstCall("LotMasks", logged);
    *zone = 0x80000000;
    *building = 0x80000000;
    *size = 0x80000000;
    *wealth = 0x80000000;
    int32_t x = 0, z = 0;
    Virtual<uint32_t>(lot, 0x54, &x, &z);
    const int32_t zoneType = Virtual<int32_t>(Global<void*>(addr::SC4_ZoneManager), 0x30, x, z);
    if (zoneType >= 1 && zoneType <= 3)
        *zone = 1;
    else if (zoneType >= 4 && zoneType <= 6)
        *zone = 2;
    else if (zoneType >= 7 && zoneType <= 9)
        *zone = 4;
    const uint32_t lotWealth = Virtual<uint32_t>(lot, 0x8C);
    if (lotWealth <= 1)
        *wealth = 1;
    else if (lotWealth == 4)
        *wealth = 4;
    else if (lotWealth <= 6)
        *wealth = 2;
    void* const occupant = Virtual<void*>(lot, 0xC0);
    if (!occupant) {
        *size = 0x100;
        return 1;
    }
    const auto* const levels = Virtual<const uint32_t*>(occupant, 0x2C);
    *building = 1u << ((levels[1] - 1) & 31);
    *size = levels[0] ? 1u << ((levels[0] - 1) & 31) : 0x100u;
    return 1;
}

// 0x006A1A40: a crime type for the lot, drawn by weight (generator 0x00B48630)
// among those whose four masks match, recorded as an incident; its message
// (0x0A159D95) is sent, and for some types the lot's building is told. The
// type's strength is returned (0: none matched).
uint8_t __fastcall CreateIncident(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("CreateIncident", logged);
    uint32_t masks[4];
    reinterpret_cast<MasksFn>(addr::cSC4CrimeSimulator_LotMasks)(lot, &masks[0], &masks[1], &masks[2], &masks[3]);
    uint32_t typeCount;
    CrimeType* const* types = CrimeTypes(&typeCount);
    Vector picked{};
    float total = 0.0f;
    CrimeType* type = nullptr;
    for (uint32_t i = 0; i < typeCount; ++i) {
        type = types[i];
        if (!(type->masks[0] & masks[0]) || !(type->masks[1] & masks[1]) || !(type->masks[2] & masks[2]) ||
            !(type->masks[3] & masks[3]))
            continue;
        total = static_cast<float>(static_cast<double>(total) + type->weight);
        uint32_t index = i;
        if (picked.end != picked.capacity) {
            if (picked.end)
                *reinterpret_cast<uint32_t*>(picked.end) = index;
            picked.end += 4;
        } else {
            char allocator;
            PtrInsert(&picked, picked.end, &index, &allocator, 1, 1);
        }
    }
    if (picked.begin == picked.end) {
        if (picked.begin)
            Free(picked.begin);
        return 0;
    }
    double draw = RandomRange(Random(), Global<double>(addr::SC4_DoubleZero), total);
    const auto* const indices = reinterpret_cast<const uint32_t*>(picked.begin);
    const uint32_t pickedCount = static_cast<uint32_t>((picked.end - picked.begin) >> 2);
    if (draw >= 0.0) {
        for (uint32_t k = 0; k < pickedCount;) {
            type = types[indices[k]];
            draw -= type->weight;
            ++k;
            if (!(draw >= 0.0))
                break;
        }
    }

    auto* const incident = static_cast<Incident*>(OperatorNew(0x2C));
    if (incident) {
        incident->occupant = nullptr;
        incident->nameBegin = nullptr;
        incident->nameEnd = nullptr;
        incident->nameCapacity = nullptr;
        char* const buffer = static_cast<char*>(StlAllocate(8));
        incident->nameBegin = buffer;
        incident->nameEnd = buffer;
        incident->nameCapacity = buffer + 8;
        buffer[0] = 0;
        incident->nameVtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        incident->nameZero = 0;
        incident->key[0] = incident->key[1] = incident->key[2] = 0;
    }
    void* const previous = incident->occupant;
    if (lot != previous) {
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        incident->occupant = lot;
        if (previous)
            Virtual<uint32_t>(previous, 0x8);
    }
    incident->crimeType = type->id;
    char tag;
    if (&type->nameBegin != &incident->nameBegin)
        reinterpret_cast<AssignFn>(addr::SC4String_AssignRange)(&incident->nameBegin, type->nameBegin, type->nameEnd,
                                                                 &tag);
    std::memcpy(incident->key, type->key, sizeof incident->key);
    incident->visualsCreated = 0;
    PushPointer(&At<Vector>(self, field::kIncidents), incident);

    if (type->notify) {
        if (void* const building = Virtual<void*>(lot, 0xC0)) {
            if (void* const layer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120)) {
                void* const source = Virtual<void*>(layer, 0x1C, 0x621CDA33u);
                void* target = nullptr;
                Virtual<uint8_t>(source, 0x0, 0x49B43F57u, &target);
                if (target) {
                    Virtual<uint32_t>(target, 0x4);
                    Virtual<uint32_t>(target, 0x8);
                    const uint32_t id = Virtual<uint32_t>(building, 0xC);
                    Virtual<uint32_t>(target, 0x10, id);
                    Virtual<uint32_t>(target, 0x24, 2u);
                    Virtual<uint32_t>(target, 0x8);
                }
            }
        }
    }

    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x14, 0x0A159D95u);
    Virtual<uint32_t>(message, 0x2C, incident->crimeType);
    Virtual<uint32_t>(message, 0x84, incident->occupant);
    auto* const name = static_cast<uint32_t*>(OperatorNew(0x14));
    if (name) {
        reinterpret_cast<CopyFn>(addr::SC4String_CopyBuffer)(name + 1, &incident->nameBegin, 0, 0xFFFFFFFF, &tag);
        name[0] = static_cast<uint32_t>(addr::SC4String_Vtable);
        name[4] = 0;
    }
    Virtual<uint32_t>(message, 0x78, name);
    Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
    const uint8_t strength = type->strength;
    if (picked.begin)
        Free(picked.begin);
    return strength;
}

// 0x006A1DE0 (state 4): `count` tries, each at a random lot (lot manager
// 0x00B43D08, slot 13): the police power over the lot (police grid, slot 8
// over its rectangle) less the effect grid's, through the chance curve
// (+0x14C), against a draw (0x00B48630); when the draw is not below it, an
// incident (0x006A1A40) whose strength is added to the crime grid over the
// lot (saturating), keeping the hotspot. Returns the incidents placed.
uint32_t __fastcall GenerateCrimes(void* self, void*, int32_t count)
{
    static bool logged = false;
    FirstCall("GenerateCrimes", logged);
    uint8_t* const* const rows = Rows<uint8_t>(At<void*>(self, field::kCrimeGrid));
    uint32_t placed = 0;
    for (int32_t tries = count; tries > 0; --tries) {
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x34);
        if (!lot)
            continue;
        const uint8_t shift = Virtual<uint8_t>(At<void*>(self, field::kCrimeGrid), 0x2C);
        int32_t rect[4];
        Virtual<uint32_t>(lot, 0x38, rect);
        void* const policeGrid = Virtual<void*>(At<void*>(self, field::kPolice), 0x14);
        const uint8_t police =
            Virtual<uint8_t>(policeGrid, 0x20, rect[0], rect[1], rect[2] - rect[0] + 1, rect[3] - rect[1] + 1);
        const uint8_t effect = Virtual<uint8_t>(At<void*>(self, field::kEffectGrid), 0x20, rect[0], rect[1],
                                                rect[2] - rect[0] + 1, rect[3] - rect[1] + 1);
        const int32_t protection = static_cast<int32_t>(police) - static_cast<int32_t>(effect);
        const float threshold = static_cast<float>(
            EvaluateCurve(static_cast<char*>(self) + field::kChanceCurve, static_cast<float>(protection)));
        const double draw = RandomUnit(Random());
        if (static_cast<double>(threshold) > draw)
            continue;
        const uint8_t strength =
            reinterpret_cast<IncidentFn>(addr::cSC4CrimeSimulator_CreateIncident)(self, lot);
        if (!strength)
            continue;
        const int32_t x0 = rect[0] >> shift, z0 = rect[1] >> shift, x1 = rect[2] >> shift, z1 = rect[3] >> shift;
        for (int32_t x = x0; x <= x1; ++x)
            for (int32_t z = z0; z <= z1; ++z) {
                uint8_t& cell = rows[3 * x][z];
                const int32_t sum = cell + strength;
                const uint8_t value = static_cast<uint8_t>(sum > 0xFF ? 0xFF : (sum < 0 ? 0 : sum));
                cell = value;
                if (static_cast<int32_t>(value) > At<int32_t>(self, field::kHotspotValue)) {
                    At<int32_t>(self, field::kHotspotValue) = value;
                    At<int32_t>(self, field::kHotspotX) = x;
                    At<int32_t>(self, field::kHotspotZ) = z;
                }
            }
        ++placed;
    }
    return placed;
}

// --- The step -------------------------------------------------------------------

// 0x006A1FD0: one state per call (+0x90), from 0 once initialised through
// 5, then 6 until MonthPassed sets 0 again.
void __fastcall Step(void* self, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("Step", logged);
    uint8_t& state = At<uint8_t>(self, field::kState);
    switch (state) {
    case 0:
        if (At<uint8_t>(self, field::kInitialized))
            state = 1;
        return;
    case 1:
        reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_CountPeople)(self);
        reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_ComputeFactors)(self);
        state = 2;
        return;
    case 2:
        At<uint32_t>(self, field::kExpected) = reinterpret_cast<ExpectedFn>(addr::cSC4CrimeSimulator_ExpectedCrimes)(self);
        state = 3;
        return;
    case 3: {
        // The expected crimes over the criminals through the excess curve,
        // plus the unjailed criminals times +0x170; then times the ordinance
        // factor, rounded to nearest through a float.
        void* const police = Global<void*>(addr::SC4_PoliceSimulator);
        const uint32_t criminals = Virtual<uint32_t>(police, 0x78);
        const int32_t unjailed = Virtual<int32_t>(police, 0x6C);
        const uint32_t expected = At<uint32_t>(self, field::kExpected);
        int32_t target;
        if (expected > criminals) {
            const int32_t excess = static_cast<int32_t>(expected - criminals);
            const int32_t fromExcess =
                Ftol(EvaluateCurve(static_cast<char*>(self) + field::kExcessCurve, static_cast<float>(excess)));
            target = fromExcess +
                     Ftol(static_cast<double>(unjailed) * At<float>(self, field::kUnjailedFactor));
        } else {
            target = Ftol(static_cast<double>(unjailed) * At<float>(self, field::kUnjailedFactor));
        }
        double value = static_cast<double>(target);
        if (target < 0)
            value += Global<float>(addr::SC4_FloatTwoTo32);
        const float rounded = static_cast<float>(value * At<float>(self, field::kOrdinanceFactor) +
                                                 Global<float>(addr::SC4_FloatRoundMagic));
        uint32_t bits;
        std::memcpy(&bits, &rounded, sizeof bits);
        state = 4;
        At<uint32_t>(self, field::kTarget) = bits + 0xB4C00000u;
        return;
    }
    case 4:
        reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_Decay)(self);
        reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_ClearIncidents)(self);
        At<uint32_t>(self, field::kMonthlyCrimes) = reinterpret_cast<GenerateFn>(addr::cSC4CrimeSimulator_GenerateCrimes)(
            self, At<int32_t>(self, field::kTarget));
        Virtual<uint8_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x98);
        reinterpret_cast<SelfFn>(addr::cSC4CrimeSimulator_UpdateAverage)(self);
        state = 5;
        return;
    case 5: {
        void* const message = NewMessage();
        Virtual<uint32_t>(message, 0x14, 0x88EA4DFFu);
        Virtual<uint32_t>(message, 0x30, self);
        Virtual<uint32_t>(message, 0x84, self);
        Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0u);
        Virtual<uint32_t>(message, 0x8);
        state = 6;
        return;
    }
    default:
        return;
    }
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
struct Detour {
    std::uintptr_t address;
    Fn ours;
    Fn* original;
};

using StepFn = void(__fastcall*)(void*, void*, uint32_t);
using HelperFn = void(__fastcall*)(void*, void*);
using ExpectedOursFn = uint32_t(__fastcall*)(void*, void*);
using GenerateOursFn = uint32_t(__fastcall*)(void*, void*, int32_t);
using IncidentOursFn = uint8_t(__fastcall*)(void*, void*, void*);

StepFn g_originalStep = nullptr;
HelperFn g_originalCountPeople = nullptr, g_originalFactors = nullptr, g_originalDecay = nullptr,
         g_originalClear = nullptr, g_originalAverage = nullptr;
ExpectedOursFn g_originalExpected = nullptr;
GenerateOursFn g_originalGenerate = nullptr;
IncidentOursFn g_originalIncident = nullptr;
MasksFn g_originalMasks = nullptr;

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool InstallStep(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4CrimeSimulator_Step, &Step, &g_originalStep);
}

bool InstallHelpers(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4CrimeSimulator_CountPeople, &CountPeople, &g_originalCountPeople) &&
           Install(addr::cSC4CrimeSimulator_ComputeFactors, &ComputeFactors, &g_originalFactors) &&
           Install(addr::cSC4CrimeSimulator_ExpectedCrimes, &ExpectedCrimes, &g_originalExpected) &&
           Install(addr::cSC4CrimeSimulator_Decay, &Decay, &g_originalDecay) &&
           Install(addr::cSC4CrimeSimulator_ClearIncidents, &ClearIncidents, &g_originalClear) &&
           Install(addr::cSC4CrimeSimulator_UpdateAverage, &UpdateAverage, &g_originalAverage);
}

bool InstallGenerate(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4CrimeSimulator_GenerateCrimes, &GenerateCrimes, &g_originalGenerate) &&
           Install(addr::cSC4CrimeSimulator_CreateIncident, &CreateIncident, &g_originalIncident) &&
           Install(addr::cSC4CrimeSimulator_LotMasks, &LotMasks, &g_originalMasks);
}

const bool g_registeredStep = registry::Register({
    "simulators.crime.step",
    "simulators/crime",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_Step,
    "Our crime step: the month's pass in six states (docs/crime.md)",
    false,
    InstallStep,
});

const bool g_registeredHelpers = registry::Register({
    "simulators.crime.helpers",
    "simulators/crime",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_CountPeople,
    "Our crime step's helpers: people counts, demand factors, expected crimes, decay, the average (docs/crime.md)",
    false,
    InstallHelpers,
});

const bool g_registeredGenerate = registry::Register({
    "simulators.crime.generate",
    "simulators/crime",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_GenerateCrimes,
    "Our crime placement: lots, crime types by weight, incidents (docs/crime.md)",
    false,
    InstallGenerate,
});

} // namespace
} // namespace opensc4::crime_sim
