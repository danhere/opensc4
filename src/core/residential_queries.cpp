// Our cSC4ResidentialSimulator's queries (build 641; docs/residential.md):
// strikes and their chances, the school and health ratings and totals, EQ,
// HQ and life expectancy by cell and city-wide, the auto budget flags.
// Every function is a replacement on the original object (slots of the
// primary vtable 0x00AB2C10); the helpers they call inside the class are
// plain functions here.
//
// Fields used here beyond residential_sim.h's (offsets from the object's
// start), to merge into its namespace field:
//   kProximity1 = 0x38, kProximity2 = 0x3C, kProximity3 = 0x40 (by wealth)
//   kPopulationGrids = 0x48 (+0x4C, +0x50), kEQGrids = 0x7C (+0x80, +0x84),
//   kHQGrids = 0x88 (+0x8C, +0x90)
//   kSchoolList = 0x19C, kHospitalList = 0x1A0 (list heads, counted with the
//     buildings)
// The strike blocks (+0xD4 health, +0x134 school): +0x24 the strike chance
// curve (SC4Curve), +0x44 uint8 strike, +0x48 the day it began, +0x5C the
// message type posted when it ends.
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
constexpr std::size_t kProximity1 = 0x38;
constexpr std::size_t kProximity2 = 0x3C;
constexpr std::size_t kProximity3 = 0x40;
constexpr std::size_t kSchoolList = 0x19C;
constexpr std::size_t kHospitalList = 0x1A0;
} // namespace local

// The strike blocks.
namespace block {
constexpr std::size_t kCurve = 0x24;
constexpr std::size_t kStrike = 0x44;
constexpr std::size_t kStrikeDay = 0x48;
constexpr std::size_t kEndMessage = 0x5C;
} // namespace block

constexpr uint32_t kIidExemplar = 0xA52160F5;
constexpr uint32_t kAutoBudgetProperty = 0x6C05FC4D;

// The residential tuning exemplar.
struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
constexpr ResourceKey kTuningKey{0x6534284A, 0xE7E2C2DB, 0x69189C84};

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using SetBoolFn = uint32_t(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using EvaluateFn = double(__thiscall*)(void* curve, float x);

void Free(void* block) { reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }

uint32_t DefaultPercentage() { return Global<uint32_t>(addr::cSC4ResidentialSimulator_DefaultPercentage); }
uint32_t PercentageOne() { return Global<uint32_t>(addr::cSC4ResidentialSimulator_FullFunding); }

// The element count of a vector of 4-byte elements (begin, end): SAR 2.
uint32_t Count(void* const* vector)
{
    return static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(vector[1]) - reinterpret_cast<uintptr_t>(vector[0])) >> 2);
}

float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t AsBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The truncating float-to-int conversion (0x009EEF04).
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// --- The interface -----------------------------------------------------------

// Slot 5 (0x006F6F90): the proximity map of a wealth (1-3), else none.
void* __fastcall GetProximityMap(void* self, void*, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetProximityMap", logged);
    switch (wealth) {
    case 1: return At<void*>(self, local::kProximity1);
    case 2: return At<void*>(self, local::kProximity2);
    case 3: return At<void*>(self, local::kProximity3);
    default: return nullptr;
    }
}

// Slot 6 (0x006F6F10): the school strike flag.
uint8_t __fastcall SchoolIsOnStrike(void* self, void*)
{
    static bool logged = false;
    FirstCall("SchoolIsOnStrike", logged);
    return At<uint8_t>(self, field::kSchoolStrike) != 0;
}

// Slot 7 (0x006F6F20): the health strike flag.
uint8_t __fastcall HealthIsOnStrike(void* self, void*)
{
    static bool logged = false;
    FirstCall("HealthIsOnStrike", logged);
    return At<uint8_t>(self, field::kHealthStrike) != 0;
}

// Slot 16 (0x006F6F70): the population.
int32_t __fastcall GetPopulation(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetPopulation", logged);
    return At<int32_t>(self, field::kPopulation);
}

// --- Strikes -----------------------------------------------------------------

// 0x006F8570: ends a block's strike: clears the flag and its day, and posts a
// new message (RZ operator new, 0x2C bytes) of the block's end type with 0
// (its slot 16) and the strike's length in days (its slot 21) to the message
// server (its slot 4). False when there was no strike.
uint8_t EndStrike(char* strike, uint32_t days)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* message);
    if (!At<uint8_t>(strike, block::kStrike))
        return 0;
    At<uint8_t>(strike, block::kStrike) = 0;
    At<uint32_t>(strike, block::kStrikeDay) = 0;
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, At<uint32_t>(strike, block::kEndMessage));
    void* const messages = Global<void*>(addr::SC4_MessageServer);
    Virtual<void>(message, 0x40, 0u);
    Virtual<void>(message, 0x54, days);
    Virtual<uint8_t>(messages, 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
    return 1;
}

// Today's day number: the city's slot 50, its slot 7, its slot 26.
uint32_t Today(void* self)
{
    void* const a = Virtual<void*>(At<void*>(self, field::kCity), 0xC8);
    void* const b = Virtual<void*>(a, 0x1C);
    return Virtual<uint32_t>(b, 0x68);
}

// Slot 8 (0x006F9470): ends the school strike, if there is one (slot 6).
uint8_t __fastcall EndSchoolStrike(void* self, void*)
{
    static bool logged = false;
    FirstCall("EndSchoolStrike", logged);
    if (!Virtual<uint8_t>(self, 0x18))
        return 0;
    const uint32_t today = Today(self);
    return EndStrike(static_cast<char*>(self) + field::kSchool, today - At<uint32_t>(self, field::kSchoolStrikeDay));
}

// Slot 9 (0x006F94B0): ends the health strike, if there is one (slot 7).
uint8_t __fastcall EndHealthStrike(void* self, void*)
{
    static bool logged = false;
    FirstCall("EndHealthStrike", logged);
    if (!Virtual<uint8_t>(self, 0x1C))
        return 0;
    const uint32_t today = Today(self);
    return EndStrike(static_cast<char*>(self) + field::kHealth, today - At<uint32_t>(self, field::kHealthStrikeDay));
}

// 0x006F9390: the chance that the buildings of one list strike, into *out:
// 1 when the block is on strike; else over the buildings, starting from the
// default, c = c + p - c * p, where p is, for a building of `group` (its
// slot 22), the block's curve at 100 times its `property` (float; the last
// value read stays when a building lacks it) times 0.01. A building not of
// the group combines the last p again, as the game does.
void ChanceOfStrike(float* out, char* strike, void** list, uint32_t property, uint32_t group)
{
    const uint32_t start = DefaultPercentage();
    if (At<uint8_t>(strike, block::kStrike)) {
        *reinterpret_cast<uint32_t*>(out) = PercentageOne();
        return;
    }
    float chance = AsFloat(start);
    float p = chance;
    float value = chance;
    void* const head = *list;
    for (void* node = *static_cast<void**>(head); node != head; node = *static_cast<void**>(node)) {
        void* const occupant = At<void*>(node, 8);
        if (Virtual<uint8_t>(occupant, 0x58, group)) {
            float read;
            void* const holder = Virtual<void*>(occupant, 0x18);
            if (reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(holder, property, &read))
                value = read;
            const float x = static_cast<float>(static_cast<double>(value) * 100.0);
            const double curve =
                reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(strike + block::kCurve, x);
            p = static_cast<float>(curve * static_cast<double>(0.01f));
        }
        const double c = chance;
        chance = static_cast<float>((c + p) - c * p);
    }
    *out = chance;
}

// Slots 10 and 11: the chance of a strike over the building lists, into
// *out (returned): 1 on strike, else c = p + c - p * c over the lists.
float* ChanceOf(void* self, float* out, std::size_t onStrikeSlot, std::size_t lists, std::size_t strike,
                uint32_t property, uint32_t group)
{
    const uint32_t start = DefaultPercentage();
    if (Virtual<uint8_t>(self, onStrikeSlot)) {
        *reinterpret_cast<uint32_t*>(out) = PercentageOne();
        return out;
    }
    void** const vector = &At<void*>(self, lists);
    if (Count(vector) == 0) {
        *reinterpret_cast<uint32_t*>(out) = start;
        return out;
    }
    float chance = AsFloat(start);
    float p;
    for (uint32_t i = 0; i < Count(vector); ++i) {
        ChanceOfStrike(&p, static_cast<char*>(self) + strike, static_cast<void**>(vector[0]) + i, property, group);
        const double b = p;
        chance = static_cast<float>((b + chance) - b * chance);
    }
    *out = chance;
    return out;
}

// Slot 10 (0x006F9670): the chance of a school strike (SC4Percentage, by
// pointer): schools of group 0x1A03, property 0x292453C5.
float* __fastcall ChanceOfSchoolStrike(void* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("ChanceOfSchoolStrike", logged);
    return ChanceOf(self, out, 0x18, field::kSchoolBuildings, field::kSchool, 0x292453C5u, 0x1A03u);
}

// Slot 11 (0x006F9730): the chance of a health strike: hospitals of group
// 0x1A02, property 0x092453CB.
float* __fastcall ChanceOfHealthStrike(void* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("ChanceOfHealthStrike", logged);
    return ChanceOf(self, out, 0x1C, field::kHospitalBuildings, field::kHealth, 0x092453CBu, 0x1A02u);
}

// --- Ratings and totals ------------------------------------------------------

// The query data of a building (0x40 bytes on the game's stack): a cIGZString
// (SC4String) at +0, then numbers the query slots fill. The game leaves
// +0x14-0x24, +0x30, +0x38 and +0x3C unset; they are 0 here.
struct QueryData {
    uint32_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
    uint32_t f14;
    uint32_t f18;
    uint32_t f1c;
    uint32_t f20;
    uint32_t f24;
    float f28;
    uint32_t f2c;
    uint32_t f30;
    uint32_t f34;
    uint32_t f38;
    uint32_t f3c;
};
static_assert(sizeof(QueryData) == 0x40);

// The game's inline construction: an empty string with an 8-byte buffer.
void InitQueryData(QueryData& data)
{
    data = QueryData{};
    char* const buffer = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(8));
    data.begin = buffer;
    data.end = buffer;
    data.capacity = buffer + 8;
    *buffer = 0;
    data.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    data.zero = 0;
    data.f28 = 0.0f;
    data.f2c = 0;
    data.f34 = 0;
}

void FreeQueryData(QueryData& data)
{
    data.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (data.begin)
        Free(data.begin);
}

// 0x006F9F20 and 0x006F9DD0: the rating (SC4Percentage, into *out): the sum
// of +0x18 of every building's query data (`querySlot`) over the population,
// clamped to 1; the default without buildings, 1 without population.
float* Rating(void* self, float* out, std::size_t lists, std::size_t querySlot)
{
    QueryData data;
    InitQueryData(data);
    void** const vector = &At<void*>(self, lists);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < Count(vector); ++i) {
        void* const head = static_cast<void**>(vector[0])[i];
        for (void* node = *static_cast<void**>(head); node != head; node = *static_cast<void**>(node)) {
            Virtual<uint8_t>(self, querySlot, At<void*>(node, 8), &data);
            sum += data.f18;
        }
    }
    uint32_t bits;
    if (sum == 0) {
        bits = DefaultPercentage();
    } else if (Virtual<int32_t>(self, 0x40) == 0) {
        bits = PercentageOne();
    } else {
        const float total = static_cast<float>(static_cast<double>(sum));
        const int32_t population = Virtual<int32_t>(self, 0x40);
        bits = AsBits(static_cast<float>(static_cast<double>(total) / population));
    }
    if (AsFloat(bits) > Global<float>(addr::cSC4ResidentialSimulator_FullFunding))
        bits = PercentageOne();
    *reinterpret_cast<uint32_t*>(out) = bits;
    FreeQueryData(data);
    return out;
}

// Slot 12 (0x006F9F20): the school system's rating (school query, slot 26).
float* __fastcall GetSchoolSystemRating(void* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("GetSchoolSystemRating", logged);
    return Rating(self, out, field::kSchoolBuildings, 0x68);
}

// Slot 13 (0x006F9DD0): the health system's rating (hospital query, slot 27).
float* __fastcall GetHealthSystemRating(void* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("GetHealthSystemRating", logged);
    return Rating(self, out, field::kHospitalBuildings, 0x6C);
}

// 0x006FA070: totals over the buildings of the lists in `vector` that are
// in any of `groups` (their slot 22; the game reads `groups` without a null
// check): +0x14-0x24 summed as integers and +0x28 as a float from each
// building's query data (slot 26 for schools, 27 for hospitals) into `out`.
// Returns the number of buildings counted.
uint32_t Totals(void* self, void** vector, char* out, uint8_t school, void** groups)
{
    QueryData data;
    InitQueryData(data);
    At<uint32_t>(out, 0x14) = 0;
    At<uint32_t>(out, 0x18) = 0;
    At<uint32_t>(out, 0x1C) = 0;
    At<uint32_t>(out, 0x20) = 0;
    At<uint32_t>(out, 0x24) = 0;
    At<uint32_t>(out, 0x28) = DefaultPercentage();
    uint32_t count = 0;
    for (uint32_t i = 0; i < Count(vector); ++i) {
        void* const head = static_cast<void**>(vector[0])[i];
        for (void* node = *static_cast<void**>(head); node != head; node = *static_cast<void**>(node)) {
            void* const occupant = At<void*>(node, 8);
            bool found = false;
            for (uint32_t j = 0; j < Count(groups); ++j) {
                if (found)
                    break;
                if (Virtual<uint8_t>(occupant, 0x58, static_cast<uint32_t*>(groups[0])[j]))
                    found = true;
            }
            if (!found)
                continue;
            Virtual<uint8_t>(self, school ? 0x68 : 0x6C, occupant, &data);
            At<uint32_t>(out, 0x14) += data.f14;
            At<uint32_t>(out, 0x20) += data.f20;
            At<uint32_t>(out, 0x18) += data.f18;
            At<uint32_t>(out, 0x1C) += data.f1c;
            At<uint32_t>(out, 0x24) += data.f24;
            At<float>(out, 0x28) = static_cast<float>(static_cast<double>(data.f28) + At<float>(out, 0x28));
            ++count;
        }
    }
    FreeQueryData(data);
    return count;
}

// Slot 14 (0x006FA240): the school totals (0x006FA070 on the school lists).
uint32_t __fastcall GetSchoolSystemTotals(void* self, void*, void* data, void* groups)
{
    static bool logged = false;
    FirstCall("GetSchoolSystemTotals", logged);
    return Totals(self, &At<void*>(self, field::kSchoolBuildings), static_cast<char*>(data), 1,
                  static_cast<void**>(groups));
}

// Slot 15 (0x006FA260): the hospital totals.
uint32_t __fastcall GetHospitalSystemTotals(void* self, void*, void* data, void* groups)
{
    static bool logged = false;
    FirstCall("GetHospitalSystemTotals", logged);
    return Totals(self, &At<void*>(self, field::kHospitalBuildings), static_cast<char*>(data), 0,
                  static_cast<void**>(groups));
}

// 0x006F9590 and 0x006F9600: the buildings in the lists plus the extra list.
int32_t BuildingCount(void* self, std::size_t lists, std::size_t extra)
{
    void** const vector = &At<void*>(self, lists);
    int32_t total = 0;
    for (uint32_t i = 0; i < Count(vector); ++i) {
        void* const head = static_cast<void**>(vector[0])[i];
        int32_t n = 0;
        for (void* node = *static_cast<void**>(head); node != head; node = *static_cast<void**>(node))
            ++n;
        total += n;
    }
    void* const head = At<void*>(self, extra);
    int32_t n = 0;
    for (void* node = *static_cast<void**>(head); node != head; node = *static_cast<void**>(node))
        ++n;
    return n + total;
}

// Slot 46 (0x006F9590): the number of school buildings.
int32_t __fastcall GetSchoolBuildingCount(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetSchoolBuildingCount", logged);
    return BuildingCount(self, field::kSchoolBuildings, local::kSchoolList);
}

// Slot 48 (0x006F9600): the number of hospital buildings.
int32_t __fastcall GetHospitalBuildingCount(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetHospitalBuildingCount", logged);
    return BuildingCount(self, field::kHospitalBuildings, local::kHospitalList);
}

// --- EQ and HQ ---------------------------------------------------------------

// 0x006F6FC0: the population-weighted average of three grids (EQ or HQ) at
// a cell: (q0 p0 + float(q1 p1 + float(q2 p2))) / (p0 + p1 + p2), the
// population sum as uint16; 0 without population. Returned unrounded (ST0).
double CellAverage(void* const* grids, void* const* population, uint32_t x, uint32_t z)
{
    uint16_t sum = Virtual<uint16_t>(population[0], 0x58, x, z);
    sum = static_cast<uint16_t>(sum + Virtual<uint16_t>(population[1], 0x58, x, z));
    sum = static_cast<uint16_t>(sum + Virtual<uint16_t>(population[2], 0x58, x, z));
    if (sum == 0)
        return Global<float>(addr::SC4_FloatZero);
    const float p2 = static_cast<float>(Virtual<uint16_t>(population[2], 0x58, x, z));
    const float s2 = static_cast<float>(Virtual<double>(grids[2], 0x58, x, z) * p2);
    const float p1 = static_cast<float>(Virtual<uint16_t>(population[1], 0x58, x, z));
    const float s1 = static_cast<float>(Virtual<double>(grids[1], 0x58, x, z) * p1 + s2);
    const float p0 = static_cast<float>(Virtual<uint16_t>(population[0], 0x58, x, z));
    return (Virtual<double>(grids[0], 0x58, x, z) * p0 + s1) / static_cast<int32_t>(sum);
}

void* const* Grids(void* self, std::size_t offset) { return &At<void*>(self, offset); }

// 0x006F7720 and 0x006F7870: the city-wide average: the sum over all cells
// of the population-weighted values over the population (slot 16); 0
// without population. Returned unrounded.
double GlobalAverage(void* self, std::size_t grids)
{
    if (Virtual<int32_t>(self, 0x40) == 0)
        return Global<float>(addr::SC4_FloatZero);
    const uint32_t width = Virtual<uint32_t>(At<void*>(self, grids), 0x30);
    const uint32_t height = Virtual<uint32_t>(At<void*>(self, grids), 0x34);
    float sum = 0.0f;
    for (uint32_t x = 0; x < width; ++x) {
        for (uint32_t z = 0; z < height; ++z) {
            void* const* const q = Grids(self, grids);
            void* const* const p = Grids(self, field::kPopulationGrids);
            void* const q0 = q[0];
            void* const q1 = q[1];
            void* const q2 = q[2];
            void* const p0 = p[0];
            void* const p1 = p[1];
            void* const p2 = p[2];
            const float f0 = static_cast<float>(Virtual<uint16_t>(p0, 0x58, x, z));
            const float t0 = static_cast<float>(Virtual<double>(q0, 0x58, x, z) * f0);
            const float f1 = static_cast<float>(Virtual<uint16_t>(p1, 0x58, x, z));
            const float t1 = static_cast<float>(Virtual<double>(q1, 0x58, x, z) * f1 + t0);
            const float f2 = static_cast<float>(Virtual<uint16_t>(p2, 0x58, x, z));
            sum = static_cast<float>((Virtual<double>(q2, 0x58, x, z) * f2 + t1) + sum);
        }
    }
    const int32_t population = Virtual<int32_t>(self, 0x40);
    return static_cast<double>(sum) / population;
}

// Slot 32 (0x006F7720): the city's EQ.
double __fastcall GetGlobalEQ(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetGlobalEQ", logged);
    return GlobalAverage(self, field::kEQGrids);
}

// Slot 33 (0x006F7870): the city's HQ.
double __fastcall GetGlobalHQ(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetGlobalHQ", logged);
    return GlobalAverage(self, field::kHQGrids);
}

// 0x006F79C0 and 0x006F7A10: the average at a world cell (divided by the
// first grid's cell size, its slot 10, z first).
double CellValue(void* self, std::size_t grids, uint32_t x, uint32_t z)
{
    void* const grid = At<void*>(self, grids);
    const uint32_t gz = z / Virtual<uint32_t>(grid, 0x28);
    const uint32_t gx = x / Virtual<uint32_t>(grid, 0x28);
    return CellAverage(Grids(self, grids), Grids(self, field::kPopulationGrids), gx, gz);
}

// Slot 35 (0x006F79C0): the EQ at a cell.
double __fastcall GetCellEQ(void* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetCellEQ", logged);
    return CellValue(self, field::kEQGrids, x, z);
}

// Slot 36 (0x006F7A10): the HQ at a cell.
double __fastcall GetCellHQ(void* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetCellHQ", logged);
    return CellValue(self, field::kHQGrids, x, z);
}

// 0x006F7A60 and 0x006F7AB0: one wealth's grid value at a world cell; the
// wealth byte minus 1 indexes the grids unchecked.
double CellValueByWealth(void* self, std::size_t grids, uint32_t x, uint32_t z, uint32_t wealth)
{
    const uint32_t gz = z / Virtual<uint32_t>(At<void*>(self, grids), 0x28);
    const uint32_t gx = x / Virtual<uint32_t>(At<void*>(self, grids), 0x28);
    const uint8_t index = static_cast<uint8_t>(static_cast<uint8_t>(wealth) - 1);
    return Virtual<double>(At<void*>(self, grids + index * 4u), 0x58, gx, gz);
}

// Slot 37 (0x006F7A60): the EQ of a wealth at a cell.
double __fastcall GetCellEQByWealth(void* self, void*, uint32_t x, uint32_t z, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetCellEQByWealth", logged);
    return CellValueByWealth(self, field::kEQGrids, x, z, wealth);
}

// Slot 38 (0x006F7AB0): the HQ of a wealth at a cell.
double __fastcall GetCellHQByWealth(void* self, void*, uint32_t x, uint32_t z, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetCellHQByWealth", logged);
    return CellValueByWealth(self, field::kHQGrids, x, z, wealth);
}

// 0x006F7380 and 0x006F7550: the cells (centres in world units) with the
// lowest and highest average among cells with population, and the values
// (starting at 200 and -200); all 0 and false without population.
uint8_t MinAndMax(void* self, std::size_t grids, uint32_t* minX, uint32_t* minZ, uint32_t* maxX, uint32_t* maxZ,
                  float* low, float* high)
{
    if (Virtual<int32_t>(self, 0x40) == 0) {
        *maxX = 0;
        *minX = 0;
        *maxZ = 0;
        *minZ = 0;
        *reinterpret_cast<uint32_t*>(low) = 0;
        *reinterpret_cast<uint32_t*>(high) = 0;
        return 0;
    }
    const uint32_t width = Virtual<uint32_t>(At<void*>(self, grids), 0x30);
    const uint32_t height = Virtual<uint32_t>(At<void*>(self, grids), 0x34);
    *low = 200.0f;
    *high = -200.0f;
    // Unset in the game when no cell qualifies.
    uint32_t lowX = 0, lowZ = 0, highX = 0, highZ = 0;
    for (uint32_t x = 0; x < width; ++x) {
        for (uint32_t z = 0; z < height; ++z) {
            void* const* const p = Grids(self, field::kPopulationGrids);
            if (Virtual<uint16_t>(p[0], 0x58, x, z) == 0 && Virtual<uint16_t>(p[1], 0x58, x, z) == 0 &&
                Virtual<uint16_t>(p[2], 0x58, x, z) == 0)
                continue;
            const double value = CellAverage(Grids(self, grids), Grids(self, field::kPopulationGrids), x, z);
            if (value < static_cast<double>(*low)) {
                *low = static_cast<float>(value);
                lowX = x;
                lowZ = z;
            }
            if (value > static_cast<double>(*high)) {
                *high = static_cast<float>(value);
                highX = x;
                highZ = z;
            }
        }
    }
    const auto Centre = [&](uint32_t cell) {
        void* const grid = At<void*>(self, grids);
        const int32_t half = Virtual<int32_t>(grid, 0x28) / 2;
        return static_cast<uint32_t>(half) + Virtual<uint32_t>(grid, 0x28) * cell;
    };
    *minX = Centre(lowX);
    *minZ = Centre(lowZ);
    *maxX = Centre(highX);
    *maxZ = Centre(highZ);
    return 1;
}

// Slot 43 (0x006F7380): the cells of the lowest and highest EQ.
uint8_t __fastcall GetEQMinAndMaxCellCoords(void* self, void*, uint32_t* minX, uint32_t* minZ, uint32_t* maxX,
                                            uint32_t* maxZ, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetEQMinAndMaxCellCoords", logged);
    return MinAndMax(self, field::kEQGrids, minX, minZ, maxX, maxZ, low, high);
}

// Slot 44 (0x006F7550): the cells of the lowest and highest HQ.
uint8_t __fastcall GetHQMinAndMaxCellCoords(void* self, void*, uint32_t* minX, uint32_t* minZ, uint32_t* maxX,
                                            uint32_t* maxZ, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetHQMinAndMaxCellCoords", logged);
    return MinAndMax(self, field::kHQGrids, minX, minZ, maxX, maxZ, low, high);
}

// Slots 50 and 51 (0x006FEA40): the highest EQ and HQ, 200.
int32_t __fastcall GetMaxEQ(void*, void*)
{
    static bool logged = false;
    FirstCall("GetMaxEQ", logged);
    return 200;
}

// --- Curves from the tuning exemplar -----------------------------------------

struct PointVector {
    float* begin;
    float* end;
    float* capacity;
};

struct Curve {
    void* begin;
    void* f4;
    void* f8;
    void* fC;
};

// The tuning exemplar (resource manager slot 3); false without it.
bool GetTuning(void** resource)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    return Virtual<uint8_t>(resources, 0xC, &kTuningKey, kIidExemplar, resource, 0u, 0u) != 0;
}

// The game's inline curve setup: points (0, 0) and (200, `y`), then the
// exemplar's `property` over them.
void BuildCurve(Curve& curve, PointVector& points, float y, uint32_t property)
{
    using InitFn = void*(__thiscall*)(void* curve);
    using InsertFn = void(__thiscall*)(void* vector, float* where, const float* value, void* allocator,
                                       uint32_t count, uint32_t atEnd);
    using SetPointsFn = uint8_t(__thiscall*)(void* curve, const void* points);
    using LoadFn = uint8_t(__thiscall*)(void* curve, const void* key, uint32_t property);
    reinterpret_cast<InitFn>(addr::SC4Curve_Init)(&curve);
    uint8_t allocator = 0;
    float point[2] = {0.0f, 0.0f};
    points = PointVector{};
    reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(&points, nullptr, point, &allocator, 1u, 1u);
    point[0] = 200.0f;
    point[1] = y;
    if (points.end != points.capacity) {
        if (points.end) {
            points.end[0] = 200.0f;
            points.end[1] = y;
        }
        points.end += 2;
    } else {
        reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(&points, points.end, point, &allocator, 1u, 1u);
    }
    reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints)(&curve, &points);
    reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromExemplar)(&curve, &kTuningKey, property);
}

void FreeCurve(Curve& curve, PointVector& points)
{
    if (points.begin)
        Free(points.begin);
    if (curve.begin)
        Free(curve.begin);
}

double Evaluate(Curve& curve, float x) { return reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(&curve, x); }

// Slot 29 (0x006FC570): the life expectancy at a cell: the curve 0x6941A67F
// (default 0-200 to 0-200) at the cell's HQ (slot 36), truncated; 0
// without the tuning exemplar.
uint8_t __fastcall GetCellLifeExpectancy(void* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetCellLifeExpectancy", logged);
    void* resource = nullptr;
    if (!GetTuning(&resource))
        return 0;
    Curve curve;
    PointVector points;
    BuildCurve(curve, points, 200.0f, 0x6941A67F);
    const float hq = static_cast<float>(Virtual<double>(self, 0x90, x, z));
    Virtual<uint32_t>(resource, 0x8);
    const uint8_t result = static_cast<uint8_t>(Ftol(Evaluate(curve, hq)));
    FreeCurve(curve, points);
    return result;
}

// Slot 30 (0x006FC6E0): the workforce share at a cell (SC4Percentage, into
// *out): the curve 0x4941A92B (default 0-200 to 0-100) at the cell's life
// expectancy (slot 29) times 0.01; the default without the exemplar.
float* __fastcall GetCellWorkforcePercent(void* self, void*, float* out, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetCellWorkforcePercent", logged);
    void* resource = nullptr;
    if (!GetTuning(&resource)) {
        *reinterpret_cast<uint32_t*>(out) = DefaultPercentage();
        return out;
    }
    Curve curve;
    PointVector points;
    BuildCurve(curve, points, 100.0f, 0x4941A92B);
    const uint8_t expectancy = Virtual<uint8_t>(self, 0x74, x, z);
    Virtual<uint32_t>(resource, 0x8);
    *out = static_cast<float>(Evaluate(curve, static_cast<float>(expectancy)) * static_cast<double>(0.01f));
    FreeCurve(curve, points);
    return out;
}

// Slot 31 (0x006FC860): the city's workforce share: the same curve at the
// city's life expectancy (slot 34).
float* __fastcall GetGlobalWorkforcePercent(void* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("GetGlobalWorkforcePercent", logged);
    void* resource = nullptr;
    if (!GetTuning(&resource)) {
        *reinterpret_cast<uint32_t*>(out) = DefaultPercentage();
        return out;
    }
    Curve curve;
    PointVector points;
    BuildCurve(curve, points, 100.0f, 0x4941A92B);
    const uint8_t expectancy = Virtual<uint8_t>(self, 0x88);
    Virtual<uint32_t>(resource, 0x8);
    *out = static_cast<float>(Evaluate(curve, static_cast<float>(expectancy)) * static_cast<double>(0.01f));
    FreeCurve(curve, points);
    return out;
}

// Slot 34 (0x006FC9E0): the city's life expectancy: the curve 0x6941A67F
// at the city's HQ (slot 33), truncated; 0 without the exemplar.
uint8_t __fastcall GetGlobalLE(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetGlobalLE", logged);
    void* resource = nullptr;
    if (!GetTuning(&resource))
        return 0;
    Curve curve;
    PointVector points;
    BuildCurve(curve, points, 200.0f, 0x6941A67F);
    Virtual<uint32_t>(resource, 0x8);
    const float hq = static_cast<float>(Virtual<double>(self, 0x84));
    const uint8_t result = static_cast<uint8_t>(Ftol(Evaluate(curve, hq)));
    FreeCurve(curve, points);
    return result;
}

// Slot 42 (0x006F88B0): the age of new residents of a wealth (1-3): uint8
// properties 0xC929C652, 0x0929CC30, 0x0929CC36 of the tuning exemplar
// (its slot 7), defaults 25, 30, 25. The game indexes by the wealth byte
// minus 1 unchecked; other wealths give 0 here.
uint8_t __fastcall GetAverageNewAgeByWealth(void* self, void*, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetAverageNewAgeByWealth", logged);
    (void)self;
    uint8_t ages[3] = {0x19, 0x1E, 0x19};
    const uint8_t index = static_cast<uint8_t>(static_cast<uint8_t>(wealth) - 1);
    void* resource = nullptr;
    if (GetTuning(&resource)) {
        void* const holder = Virtual<void*>(resource, 0x1C);
        const auto get = reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8);
        get(holder, 0xC929C652, &ages[0]);
        get(holder, 0x0929CC30, &ages[1]);
        get(holder, 0x0929CC36, &ages[2]);
        Virtual<uint32_t>(resource, 0x8);
    }
    return index < 3 ? ages[index] : 0;
}

// --- Auto budget -------------------------------------------------------------

// Slot 52 (0x006FEA50): the schools' global auto budget flag.
uint8_t __fastcall GetGlobalAutoBudgetForSchools(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetGlobalAutoBudgetForSchools", logged);
    return At<uint8_t>(self, field::kSchoolAutoBudget);
}

// Slot 53 (0x006FEA60): sets it (returns the new byte).
uint8_t __fastcall SetGlobalAutoBudgetForSchools(void* self, void*, uint32_t on)
{
    static bool logged = false;
    FirstCall("SetGlobalAutoBudgetForSchools", logged);
    At<uint8_t>(self, field::kSchoolAutoBudget) = static_cast<uint8_t>(on);
    return static_cast<uint8_t>(on);
}

// Slot 54 (0x006FEA70): the hospitals' global auto budget flag.
uint8_t __fastcall GetGlobalAutoBudgetForHospitals(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetGlobalAutoBudgetForHospitals", logged);
    return At<uint8_t>(self, field::kHospitalAutoBudget);
}

// Slot 55 (0x006FEA80): sets it (returns the new byte).
uint8_t __fastcall SetGlobalAutoBudgetForHospitals(void* self, void*, uint32_t on)
{
    static bool logged = false;
    FirstCall("SetGlobalAutoBudgetForHospitals", logged);
    At<uint8_t>(self, field::kHospitalAutoBudget) = static_cast<uint8_t>(on);
    return static_cast<uint8_t>(on);
}

// Slot 56 (0x006F7D70): a building's auto budget: its bool property
// 0x6C05FC4D (its slot 6 is the property holder), false without it. The
// game's slot takes the occupant (gzcom-dll declares no argument).
uint8_t __fastcall GetAutoBudget(void*, void*, void* occupant)
{
    using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
    static bool logged = false;
    FirstCall("GetAutoBudget", logged);
    uint8_t value = 0;
    void* const holder = Virtual<void*>(occupant, 0x18);
    reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(holder, kAutoBudgetProperty, &value);
    return value;
}

// Slot 57 (0x006F7DA0): sets a building's auto budget property.
uint32_t __fastcall SetAutoBudget(void*, void*, void* occupant, uint32_t on)
{
    static bool logged = false;
    FirstCall("SetAutoBudget", logged);
    void* const holder = Virtual<void*>(occupant, 0x18);
    return reinterpret_cast<SetBoolFn>(addr::SC4_SetPropertyBool)(holder, kAutoBudgetProperty, on);
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
    OPENSC4_SLOT(5, 0x006F6F90, GetProximityMap),
    OPENSC4_SLOT(6, 0x006F6F10, SchoolIsOnStrike),
    OPENSC4_SLOT(7, 0x006F6F20, HealthIsOnStrike),
    OPENSC4_SLOT(8, 0x006F9470, EndSchoolStrike),
    OPENSC4_SLOT(9, 0x006F94B0, EndHealthStrike),
    OPENSC4_SLOT(10, 0x006F9670, ChanceOfSchoolStrike),
    OPENSC4_SLOT(11, 0x006F9730, ChanceOfHealthStrike),
    OPENSC4_SLOT(12, 0x006F9F20, GetSchoolSystemRating),
    OPENSC4_SLOT(13, 0x006F9DD0, GetHealthSystemRating),
    OPENSC4_SLOT(14, 0x006FA240, GetSchoolSystemTotals),
    OPENSC4_SLOT(15, 0x006FA260, GetHospitalSystemTotals),
    OPENSC4_SLOT(16, 0x006F6F70, GetPopulation),
    OPENSC4_SLOT(29, 0x006FC570, GetCellLifeExpectancy),
    OPENSC4_SLOT(30, 0x006FC6E0, GetCellWorkforcePercent),
    OPENSC4_SLOT(31, 0x006FC860, GetGlobalWorkforcePercent),
    OPENSC4_SLOT(32, 0x006F7720, GetGlobalEQ),
    OPENSC4_SLOT(33, 0x006F7870, GetGlobalHQ),
    OPENSC4_SLOT(34, 0x006FC9E0, GetGlobalLE),
    OPENSC4_SLOT(35, 0x006F79C0, GetCellEQ),
    OPENSC4_SLOT(36, 0x006F7A10, GetCellHQ),
    OPENSC4_SLOT(37, 0x006F7A60, GetCellEQByWealth),
    OPENSC4_SLOT(38, 0x006F7AB0, GetCellHQByWealth),
    OPENSC4_SLOT(42, 0x006F88B0, GetAverageNewAgeByWealth),
    OPENSC4_SLOT(43, 0x006F7380, GetEQMinAndMaxCellCoords),
    OPENSC4_SLOT(44, 0x006F7550, GetHQMinAndMaxCellCoords),
    OPENSC4_SLOT(46, 0x006F9590, GetSchoolBuildingCount),
    OPENSC4_SLOT(48, 0x006F9600, GetHospitalBuildingCount),
    OPENSC4_SLOT(50, 0x006FEA40, GetMaxEQ),
    OPENSC4_SLOT(51, 0x006FEA40, GetMaxEQ),
    OPENSC4_SLOT(52, 0x006FEA50, GetGlobalAutoBudgetForSchools),
    OPENSC4_SLOT(53, 0x006FEA60, SetGlobalAutoBudgetForSchools),
    OPENSC4_SLOT(54, 0x006FEA70, GetGlobalAutoBudgetForHospitals),
    OPENSC4_SLOT(55, 0x006FEA80, SetGlobalAutoBudgetForHospitals),
    OPENSC4_SLOT(56, 0x006F7D70, GetAutoBudget),
    OPENSC4_SLOT(57, 0x006F7DA0, SetAutoBudget),
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

bool Install(registry::Mode) { return SwapSlots(kSlots, "residential sim queries"); }

const bool g_registered = registry::Register(
    {"simulators.residential.queries", "simulators/residential", registry::Kind::Vtable,
     registry::Phase::PostAppInit, addr::cSC4ResidentialSimulator_vftable,
     "Our cSC4ResidentialSimulator queries: strikes and their chances, school and health ratings and totals, "
     "EQ, HQ, life expectancy, workforce, auto budgets (docs/residential.md)",
     false, Install});

} // namespace
} // namespace opensc4::residential_sim
