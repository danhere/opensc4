// Our cSC4LandValueSimulator (build 641), reimplemented function by function
// on the original object (docs/landvalue.md). Each replacement has its own
// switch; calls between the pieces go through the game's addresses, so each
// piece is the game's or ours as configured.
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

namespace opensc4::landvalue_sim {
namespace {

// Fields of cSC4LandValueSimulator (offsets from the object's start; 0xA4 bytes).
namespace field {
constexpr std::size_t kInitialised = 0xC;      // uint8
constexpr std::size_t kMessageServer = 0x14;
constexpr std::size_t kCity = 0x18;
constexpr std::size_t kTerrain = 0x1C;
constexpr std::size_t kOccupied = 0x20;        // Service43D14: slot 12 (x, z)
constexpr std::size_t kIntrinsic = 0x24;       // cISC4SimGrid<uint8_t>*: terrain, water and altitude
constexpr std::size_t kTotal = 0x28;           // ... and the influences: the land value
constexpr std::size_t kWork = 0x2C;            // the grid the step builds in
constexpr std::size_t kSlope = 0x30;           // cISC4SimGrid<float>*
constexpr std::size_t kDirty = 0x34;           // uint8
constexpr std::size_t kState = 0x38;
constexpr std::size_t kCells = 0x3C;           // cell iterator over the land value grids
constexpr std::size_t kSlopeCells = 0x40;      // cell iterator over the slope grid
constexpr std::size_t kSum = 0x44;
constexpr std::size_t kCount = 0x48;
constexpr std::size_t kEveryCell = 0x4C;       // uint8: count cells with nothing on them too
constexpr std::size_t kHistogram = 0x50;       // vector<uint32_t>: begin, end, capacity
constexpr std::size_t kHistogramShift = 0x5C;
constexpr std::size_t kPivot = 0x60;           // float
constexpr std::size_t kRangeBelow = 0x64;      // float
constexpr std::size_t kRangeAbove = 0x68;      // float
constexpr std::size_t kTopValue = 0x6C;        // int32
constexpr std::size_t kInfluence = 0x70;       // the map node the step is at
constexpr std::size_t kAverage = 0x74;         // uint8
constexpr std::size_t kAverageAltitude = 0x78; // float
constexpr std::size_t kCellShift = 0x7C;       // uint8, then the tuning bytes to 0x82
constexpr std::size_t kLandValue = 0x7D;
constexpr std::size_t kLowBelow = 0x7F;
constexpr std::size_t kMediumBelow = 0x80;
constexpr std::size_t kWaterBonus = 0x81;
constexpr std::size_t kWaterRadius = 0x82;
constexpr std::size_t kAltitudeCurve = 0x84;   // SC4Curve* or null
constexpr std::size_t kInfluenceCurve = 0x88;  // SC4Curve* or null
constexpr std::size_t kInfluences = 0x8C;      // map header: node +0x8 left, +0xC right, +0x10 key, +0x14 factor
constexpr std::size_t kLowest = 0x98;          // float altitudes
constexpr std::size_t kHighest = 0x9C;
constexpr std::size_t kModeAltitude = 0xA0;
} // namespace field

using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using FreeFn = void(__cdecl*)(void* p);
using AllocateFn = void*(__cdecl*)(std::size_t size);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("landvalue: first %s", name);
}

// A grid's cells by row (grid slot 24 for uint8 and float grids, slot 25 for
// int16 grids; rows of 12 bytes: begin, end, capacity).
template <typename T>
T* const* Rows(void* grid, std::size_t slot = 0x60)
{
    return *Virtual<T* const* const*>(grid, slot);
}

// --- The interface: cISC4LandValueSimulator (vtable 0x00AB1670) ---------------

// 5 (0x006BD700).
uint32_t __fastcall GetLandValueType(void* self, void*, int32_t x, int32_t z)
{
    const uint8_t value = Virtual<uint8_t>(At<void*>(self, field::kTotal), 0x1C, x, z);
    if (value < At<uint8_t>(self, field::kLowBelow))
        return 1;
    return value < At<uint8_t>(self, field::kMediumBelow) ? 2 : 3;
}

// 6 (0x006BD740): the total grid's cell (grid slot 7).
uint8_t __fastcall GetLandValue(void* self, void*, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(At<void*>(self, field::kTotal), 0x1C, x, z);
}

// 7-9 (0x006BDD80 and two getters shared with other classes).
template <std::size_t Offset>
void* __fastcall GridGetter(void* self, void*)
{
    return At<void*>(self, Offset);
}

// 10 (0x006BDD90).
uint8_t __fastcall GetAverageLandValue(void* self, void*)
{
    return At<uint8_t>(self, field::kAverage);
}

// 11 (0x006BDDA0).
float __fastcall GetModeAltitude(void* self, void*)
{
    return At<float>(self, field::kModeAltitude);
}

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x006BD700, GetLandValueType),
    OPENSC4_SLOT(6, 0x006BD740, GetLandValue),
    OPENSC4_SLOT(7, 0x006BDD80, GridGetter<field::kIntrinsic>),
    OPENSC4_SLOT(8, 0x0060FB80, GridGetter<field::kTotal>),
    OPENSC4_SLOT(9, 0x006AAC50, GridGetter<field::kSlope>),
    OPENSC4_SLOT(10, 0x006BDD90, GetAverageLandValue),
    OPENSC4_SLOT(11, 0x006BDDA0, GetModeAltitude),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 13;

// Swaps every slot or none: each must still hold the game's function.
bool InstallInterface(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4LandValueSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("landvalue interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("landvalue interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("landvalue interface: %u of %d cISC4LandValueSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registeredInterface = registry::Register({
    "simulators.landvalue.interface",
    "simulators/landvalue",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_vftable,
    "Our cISC4LandValueSimulator getters on the original object (docs/landvalue.md)",
    false,
    InstallInterface,
});

// --- Helpers of the step ------------------------------------------------------

// 0x006BD540: whether any tile of the cell (the intrinsic grid's cell size,
// slots 10 and 11) has something on it (+0x20's slot 12).
uint8_t __fastcall BlockHasLot(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("BlockHasLot", logged);
    void* const grid = At<void*>(self, field::kIntrinsic);
    const uint32_t shift = Virtual<uint32_t>(grid, 0x2C) & 0xFF;
    const int32_t size = Virtual<int32_t>(grid, 0x28);
    const int32_t x0 = x << shift, z0 = z << shift;
    for (int32_t tx = x0; tx < x0 + size; ++tx)
        for (int32_t tz = z0; tz < z0 + size; ++tz)
            if (Virtual<int32_t>(At<void*>(self, field::kOccupied), 0x30, tx, tz) != 0)
                return 1;
    return 0;
}

// 0x006BD5C0: whether any tile corner of the cell, one more each way, is
// land (the terrain's slot 36).
uint8_t __fastcall BlockHasLand(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("BlockHasLand", logged);
    void* const grid = At<void*>(self, field::kIntrinsic);
    const uint32_t shift = Virtual<uint32_t>(grid, 0x2C) & 0xFF;
    const int32_t size = Virtual<int32_t>(grid, 0x28);
    const int32_t x0 = x << shift, z0 = z << shift;
    for (int32_t tx = x0; tx <= x0 + size; ++tx)
        for (int32_t tz = z0; tz <= z0 + size; ++tz)
            if (Virtual<uint8_t>(At<void*>(self, field::kTerrain), 0x90, tx, tz))
                return 1;
    return 0;
}

// 0x006BD640, the callback of ApplyInfluence: the cell plus the value
// (through the curve when there is one) times the factor, in 1..255.
struct InfluenceContext {
    float factor;
    void* curve;
};

void __cdecl AddInfluence(uint8_t* cell, int32_t value, const InfluenceContext* context)
{
    static bool logged = false;
    FirstCall("AddInfluence", logged);
    const auto v = static_cast<int16_t>(value);
    const double scaled = context->curve ? EvaluateCurve(context->curve, static_cast<float>(v)) : static_cast<double>(v);
    int32_t result = Ftol(scaled * context->factor + static_cast<double>(*cell));
    if (result < 1)
        result = 1;
    else if (result > 0xFF)
        result = 0xFF;
    *cell = static_cast<uint8_t>(result);
}

using AddInfluenceFn = void(__cdecl*)(uint8_t* cell, int32_t value, const InfluenceContext* context);

// 0x006BDA30: the callback on each cell of an 8-bit grid with the value of
// an int16 grid there; a coarser target gets the average of the cells it
// covers, a finer one the covering cell's value.
void __cdecl ApplyInfluence(void* target, void* source, AddInfluenceFn callback, const InfluenceContext* context)
{
    static bool logged = false;
    FirstCall("ApplyInfluence", logged);
    const int32_t targetShift = Virtual<int32_t>(target, 0x2C);
    const int32_t targetX = Virtual<int32_t>(target, 0x30);
    const int32_t targetZ = Virtual<int32_t>(target, 0x34);
    uint8_t* const* const targetRows = Rows<uint8_t>(target);
    const int32_t sourceShift = Virtual<int32_t>(source, 0x2C);
    const int32_t sourceX = Virtual<int32_t>(source, 0x30);
    const int32_t sourceZ = Virtual<int32_t>(source, 0x34);
    const int16_t* const* const sourceRows = Rows<int16_t>(source, 0x64);
    if (targetShift == sourceShift) {
        for (int32_t x = 0; x < targetX; ++x)
            for (int32_t z = 0; z < targetZ; ++z)
                callback(targetRows[3 * x] + z, static_cast<uint16_t>(sourceRows[3 * x][z]), context);
    } else if (targetShift > sourceShift) {
        const uint32_t d = static_cast<uint32_t>(targetShift - sourceShift) & 0xFF;
        for (int32_t x = 0; x < targetX; ++x) {
            for (int32_t z = 0; z < targetZ; ++z) {
                int32_t sum = 0;
                for (int32_t sx = x << d; sx < (x + 1) << d; ++sx)
                    for (int32_t sz = z << d; sz < (z + 1) << d; ++sz)
                        sum += sourceRows[3 * sx][sz];
                callback(targetRows[3 * x] + z, (sum >> d) >> d, context);
            }
        }
    } else {
        const uint32_t d = static_cast<uint32_t>(sourceShift - targetShift) & 0xFF;
        for (int32_t x = 0; x < sourceX; ++x) {
            for (int32_t z = 0; z < sourceZ; ++z) {
                const int32_t value = sourceRows[3 * x][z];
                for (int32_t tx = x << d; tx < (x + 1) << d; ++tx)
                    for (int32_t tz = z << d; tz < (z + 1) << d; ++tz)
                        callback(targetRows[3 * tx] + tz, value, context);
            }
        }
    }
}

// 0x006BD6B0: message 0x87CC75B3 through the message server.
void __fastcall PostUpdated(void* self, void*)
{
    static bool logged = false;
    FirstCall("PostUpdated", logged);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x87CC75B3u);
    Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// 0x006BD750: the pivot (the lower of the average and mode altitudes), the
// ranges below and above it (at least 1) and the value at the top: 100, or
// more for a city whose land rises over 100 above the pivot, at most 150.
void __fastcall UpdateAltitudeScale(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateAltitudeScale", logged);
    const float mode = At<float>(self, field::kModeAltitude);
    const float average = At<float>(self, field::kAverageAltitude);
    const double pivot = mode < average ? mode : average;
    At<float>(self, field::kPivot) = static_cast<float>(pivot);
    const double below = pivot - At<float>(self, field::kLowest);
    At<float>(self, field::kRangeBelow) = static_cast<float>(below);
    if (below < 1.0)
        At<float>(self, field::kRangeBelow) = 1.0f;
    const double above = At<float>(self, field::kHighest) - pivot;
    At<float>(self, field::kRangeAbove) = static_cast<float>(above);
    if (above < 1.0)
        At<float>(self, field::kRangeAbove) = 1.0f;
    At<int32_t>(self, field::kTopValue) = 100;
    if (At<float>(self, field::kRangeAbove) > 100.0f) {
        const float over = static_cast<float>(static_cast<double>(At<float>(self, field::kRangeAbove)) - 100.0);
        const double highest = Virtual<double>(At<void*>(self, field::kTerrain), 0x60);
        const int32_t top = Ftol(over / (highest - 100.0) * 100.0 + 100.0);
        At<int32_t>(self, field::kTopValue) = top > 150 ? 150 : top;
    }
}

// 0x006BD810: an altitude's value through the altitude curve: 0 below the
// lowest land, 0 to 50 up to the pivot, 50 to the top value above it.
uint8_t __fastcall AltitudeValue(void* self, void*, float altitude)
{
    static bool logged = false;
    FirstCall("AltitudeValue", logged);
    if (altitude < At<float>(self, field::kLowest))
        return 0;
    double value;
    if (altitude < At<float>(self, field::kPivot)) {
        value = (static_cast<double>(altitude) - At<float>(self, field::kLowest)) / At<float>(self, field::kRangeBelow) *
                50.0;
    } else {
        value = static_cast<double>(At<int32_t>(self, field::kTopValue) - 50) / At<float>(self, field::kRangeAbove) *
                    (static_cast<double>(altitude) - At<float>(self, field::kPivot)) +
                50.0;
    }
    const auto x = static_cast<float>(Ftol(value));
    return static_cast<uint8_t>(Ftol(EvaluateCurve(At<void*>(self, field::kAltitudeCurve), x)));
}

// 0x005E5390 (only the land value step calls it): each cell within the
// radius (4-neighbour steps) of a source cell holding the value gains the
// bonus less the distance times (bonus - base) / radius, kept in 0..255.
// The distances live in a temporary grid from the game's STL allocator, as
// the game's (its pool's free list order is part of the game's state).
void __cdecl SpreadFromValue(uint8_t* const* rows, int32_t width, int32_t height, uint8_t* const* sources,
                             uint8_t value, float radius, float bonus, float base)
{
    static bool logged = false;
    FirstCall("SpreadFromValue", logged);
    const auto allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
    const int32_t rounds = Ftol(radius);
    uint8_t** distance = nullptr;
    if (width > 0) {
        distance = static_cast<uint8_t**>(allocate(static_cast<std::size_t>(width) * 12));
        for (int32_t x = 0; x < width; ++x) {
            uint8_t* row = nullptr;
            if (height > 0) {
                row = static_cast<uint8_t*>(allocate(static_cast<std::size_t>(height)));
                std::memset(row, 0xFF, static_cast<std::size_t>(height));
            }
            distance[3 * x] = row;
            distance[3 * x + 1] = row + height;
            distance[3 * x + 2] = row + height;
        }
    }
    for (int32_t x = 0; x < width; ++x)
        for (int32_t z = 0; z < height; ++z)
            if (sources[3 * x][z] == value)
                distance[3 * x][z] = 0;
    for (int32_t round = 0; round < rounds; ++round) {
        const auto next = static_cast<uint8_t>(round + 1);
        for (int32_t x = 0; x < width; ++x) {
            for (int32_t z = 0; z < height; ++z) {
                if (distance[3 * x][z] != static_cast<uint8_t>(round))
                    continue;
                uint8_t* const neighbours[4] = {
                    &distance[3 * (x - 1 < 0 ? 0 : x - 1)][z],
                    &distance[3 * (x + 1 < width ? x + 1 : width - 1)][z],
                    &distance[3 * x][z - 1 < 0 ? 0 : z - 1],
                    &distance[3 * x][z + 1 < height ? z + 1 : height - 1],
                };
                for (uint8_t* const cell : neighbours)
                    if (*cell > next)
                        *cell = next;
            }
        }
    }
    const auto step = static_cast<float>((static_cast<double>(bonus) - base) / radius);
    for (int32_t x = 0; x < width; ++x) {
        for (int32_t z = 0; z < height; ++z) {
            const auto d = static_cast<float>(distance[3 * x][z]);
            if (!(d > 0.0f) || !(d < radius))
                continue;
            int32_t result = Ftol(static_cast<double>(rows[3 * x][z]) + bonus - static_cast<double>(d) * step);
            if (result < 0)
                result = 0;
            else if (result > 0xFF)
                result = 0xFF;
            rows[3 * x][z] = static_cast<uint8_t>(result);
        }
    }
    if (distance) {
        for (int32_t x = 0; x < width; ++x)
            if (distance[3 * x])
                Free(distance[3 * x]);
        Free(distance);
    }
}

using CellFn = uint8_t(__fastcall*)(void*, void*, int32_t, int32_t);
using ThisFn = void(__fastcall*)(void*, void*);
using AltitudeValueFn = uint8_t(__fastcall*)(void*, void*, float);
using ApplyInfluenceFn = void(__cdecl*)(void*, void*, AddInfluenceFn, const InfluenceContext*);
using SpreadFn = void(__cdecl*)(uint8_t* const*, int32_t, int32_t, uint8_t* const*, uint8_t, float, float, float);
CellFn g_originalBlockHasLot = nullptr;
CellFn g_originalBlockHasLand = nullptr;
AddInfluenceFn g_originalAddInfluence = nullptr;
ApplyInfluenceFn g_originalApplyInfluence = nullptr;
ThisFn g_originalPostUpdated = nullptr;
ThisFn g_originalAltitudeScale = nullptr;
AltitudeValueFn g_originalAltitudeValue = nullptr;
SpreadFn g_originalSpread = nullptr;

bool InstallHelpers(registry::Mode)
{
    const addr::Entry* lot = addr_check::Find(addr::cSC4LandValueSimulator_BlockHasLot);
    const addr::Entry* land = addr_check::Find(addr::cSC4LandValueSimulator_BlockHasLand);
    const addr::Entry* add = addr_check::Find(addr::cSC4LandValueSimulator_AddInfluence);
    const addr::Entry* apply = addr_check::Find(addr::cSC4LandValueSimulator_ApplyInfluence);
    const addr::Entry* post = addr_check::Find(addr::cSC4LandValueSimulator_PostUpdated);
    const addr::Entry* scale = addr_check::Find(addr::cSC4LandValueSimulator_UpdateAltitudeScale);
    const addr::Entry* value = addr_check::Find(addr::cSC4LandValueSimulator_AltitudeValue);
    const addr::Entry* spread = addr_check::Find(addr::LandValue_SpreadFromValue);
    return game::IsBuild641() && lot && land && add && apply && post && scale && value && spread &&
           hook::Install(*lot, &BlockHasLot, &g_originalBlockHasLot) &&
           hook::Install(*land, &BlockHasLand, &g_originalBlockHasLand) &&
           hook::Install(*add, &AddInfluence, &g_originalAddInfluence) &&
           hook::Install(*apply, &ApplyInfluence, &g_originalApplyInfluence) &&
           hook::Install(*post, &PostUpdated, &g_originalPostUpdated) &&
           hook::Install(*scale, &UpdateAltitudeScale, &g_originalAltitudeScale) &&
           hook::Install(*value, &AltitudeValue, &g_originalAltitudeValue) &&
           hook::Install(*spread, &SpreadFromValue, &g_originalSpread);
}

const bool g_registeredHelpers = registry::Register({
    "simulators.landvalue.helpers",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_ApplyInfluence,
    "Our land value step helpers: lot and land tests, the influence callback and its walk, the altitude scale "
    "and value, the waterfront spread and the update message (docs/landvalue.md)",
    false,
    InstallHelpers,
});

// --- The step (0x006BE7B0, messages 0x66956814 and 0x6695681A) -------------------

// The time budget of a pass (0x0068CC20 and the like; rdtsc based, made
// deterministic by determinism.fixed_ticks), checked every 0x400 steps.
struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};

// Starts the deadline of a pass; its step is set to 0x400 the first time.
void StartDeadline(Deadline& deadline, uint32_t budget, uint32_t bit, std::uintptr_t stepGlobal)
{
    using StartWithFn = void(__thiscall*)(Deadline* deadline, uint32_t budget, uint32_t* step);
    uint32_t& once = Global<uint32_t>(addr::LandValueSimulator_PassOnce);
    if (!(once & bit)) {
        once |= bit;
        Global<uint32_t>(stepGlobal) = 0x400;
        Global<uint32_t>(stepGlobal + 4) = 0;
    }
    reinterpret_cast<StartWithFn>(addr::SC4Deadline_StartWith)(&deadline, budget, &Global<uint32_t>(stepGlobal));
}

// The game's loop head: the count advanced, the budget checked every 0x400,
// and false when it ran out (now or before).
bool InBudget(Deadline& deadline)
{
    using CheckFn = uint8_t(__thiscall*)(Deadline* deadline);
    deadline.count += deadline.step;
    if (!(deadline.count & 0x3FF) && reinterpret_cast<CheckFn>(addr::SC4Deadline_Expired)(&deadline)) {
        deadline.timedOut = 1;
        return false;
    }
    return !deadline.timedOut;
}

void EndDeadline(Deadline& deadline)
{
    using EndFn = void(__thiscall*)(Deadline* deadline);
    reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
}

bool NextCell(void* iterator, int32_t* x, int32_t* z)
{
    using NextCellFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    return reinterpret_cast<NextCellFn>(addr::SC4CellIterator_Next)(iterator, x, z) != 0;
}

void RestartCells(void* self)
{
    using RestartFn = void(__thiscall*)(void* iterator, uint32_t a, uint32_t b);
    reinterpret_cast<RestartFn>(addr::SC4CellIterator_Restart)(At<void*>(self, field::kCells), 0, 0);
}

uint8_t HasLot(void* self, int32_t x, int32_t z)
{
    return reinterpret_cast<CellFn>(addr::cSC4LandValueSimulator_BlockHasLot)(self, nullptr, x, z);
}

uint8_t HasLand(void* self, int32_t x, int32_t z)
{
    return reinterpret_cast<CellFn>(addr::cSC4LandValueSimulator_BlockHasLand)(self, nullptr, x, z);
}

// The terrain's altitude at a cell's centre (grid slot 21, terrain slot 13).
float CellAltitude(void* self, int32_t x, int32_t z)
{
    float worldX, worldZ;
    Virtual<uint32_t>(At<void*>(self, field::kIntrinsic), 0x54, x, z, &worldX, &worldZ);
    return static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x34, worldX, worldZ));
}

void ClearSums(void* self)
{
    At<int32_t>(self, field::kSum) = 0;
    At<int32_t>(self, field::kCount) = 0;
}

void ClearHistogram(void* self)
{
    auto* const begin = At<uint32_t*>(self, field::kHistogram);
    const int32_t count = static_cast<int32_t>(At<uint32_t*>(self, field::kHistogram + 4) - begin);
    for (int32_t i = 0; i < count; ++i)
        begin[i] = 0;
}

// State 1: the work grid gets the land value of land (+0x7D) or 0.
void MarkLand(void* self, uint32_t budget)
{
    Deadline deadline;
    StartDeadline(deadline, budget, 1, addr::LandValueSimulator_Step1);
    uint8_t* const* const rows = Rows<uint8_t>(At<void*>(self, field::kWork));
    int32_t x, z;
    while (InBudget(deadline)) {
        if (!NextCell(At<void*>(self, field::kCells), &x, &z)) {
            At<int32_t>(self, field::kState) = 2;
            break;
        }
        rows[3 * x][z] = HasLand(self, x, z) ? At<uint8_t>(self, field::kLandValue) : 0;
    }
    EndDeadline(deadline);
}

// State 2: the waterfront bonus, the terrain's altitude range, sums and
// histogram cleared.
void SpreadWater(void* self)
{
    void* const work = At<void*>(self, field::kWork);
    uint8_t* const* const rows = Rows<uint8_t>(work);
    void* const intrinsic = At<void*>(self, field::kIntrinsic);
    const int32_t height = Virtual<int32_t>(intrinsic, 0x34);
    const int32_t width = Virtual<int32_t>(intrinsic, 0x30);
    reinterpret_cast<SpreadFn>(addr::LandValue_SpreadFromValue)(
        rows, width, height, rows, 0, static_cast<float>(At<uint8_t>(self, field::kWaterRadius)),
        static_cast<float>(At<uint8_t>(self, field::kWaterBonus)), 0.0f);
    At<float>(self, field::kLowest) = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x60));
    At<float>(self, field::kHighest) = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x5C));
    RestartCells(self);
    ClearSums(self);
    At<uint8_t>(self, field::kEveryCell) = 0;
    ClearHistogram(self);
    At<int32_t>(self, field::kState) = 3;
}

// State 3: the lowest and highest land, then over cells with something on
// them (every land cell when none has) the average altitude and its
// histogram; its fullest bucket gives the mode altitude.
void SurveyAltitudes(void* self, uint32_t budget)
{
    Deadline deadline;
    StartDeadline(deadline, budget, 2, addr::LandValueSimulator_Step3);
    int32_t x, z;
    for (;;) {
        if (!InBudget(deadline)) {
            EndDeadline(deadline);
            return;
        }
        if (!NextCell(At<void*>(self, field::kCells), &x, &z))
            break;
        const uint8_t land = HasLand(self, x, z);
        const float altitude = CellAltitude(self, x, z);
        if (land) {
            if (altitude < At<float>(self, field::kLowest))
                At<float>(self, field::kLowest) = altitude;
            if (altitude > At<float>(self, field::kHighest))
                At<float>(self, field::kHighest) = altitude;
        }
        if ((At<uint8_t>(self, field::kEveryCell) || HasLot(self, x, z)) && land) {
            At<int32_t>(self, field::kSum) = Ftol(static_cast<double>(At<int32_t>(self, field::kSum)) + altitude);
            ++At<int32_t>(self, field::kCount);
            auto bucket = static_cast<uint32_t>(Ftol(altitude) >> (At<uint32_t>(self, field::kHistogramShift) & 0x1F));
            if (static_cast<int32_t>(bucket) < 0)
                bucket = 0;
            uint32_t* const begin = At<uint32_t*>(self, field::kHistogram);
            const auto size = static_cast<uint32_t>(At<uint32_t*>(self, field::kHistogram + 4) - begin);
            if (bucket >= size)
                bucket = size - 1;
            ++begin[bucket];
        }
    }
    const int32_t count = At<int32_t>(self, field::kCount);
    At<float>(self, field::kAverageAltitude) = 0.0f;
    if (count < 1) {
        if (!At<uint8_t>(self, field::kEveryCell)) {
            RestartCells(self);
            ClearSums(self);
            At<uint8_t>(self, field::kEveryCell) = 1;
            ClearHistogram(self);
            At<int32_t>(self, field::kState) = 3;
            EndDeadline(deadline);
            return;
        }
    } else {
        At<float>(self, field::kAverageAltitude) =
            static_cast<float>(static_cast<double>(At<int32_t>(self, field::kSum)) / count);
    }
    uint32_t* const begin = At<uint32_t*>(self, field::kHistogram);
    const int32_t size = static_cast<int32_t>(At<uint32_t*>(self, field::kHistogram + 4) - begin);
    int32_t best = 0;
    uint32_t bestCount = 0;
    for (int32_t i = 0; i < size; ++i) {
        if (static_cast<int32_t>(begin[i]) > static_cast<int32_t>(bestCount)) {
            bestCount = begin[i];
            best = i;
        }
    }
    const uint32_t shift = At<uint32_t>(self, field::kHistogramShift);
    const int32_t mode = (1 << ((shift - 1) & 0x1F)) + (best << (shift & 0x1F));
    At<float>(self, field::kModeAltitude) = static_cast<float>(mode);
    if (static_cast<float>(mode) < At<float>(self, field::kLowest))
        At<float>(self, field::kModeAltitude) = At<float>(self, field::kLowest);
    if (At<float>(self, field::kModeAltitude) > At<float>(self, field::kHighest))
        At<float>(self, field::kModeAltitude) = At<float>(self, field::kHighest);
    reinterpret_cast<ThisFn>(addr::cSC4LandValueSimulator_UpdateAltitudeScale)(self, nullptr);
    auto* const iterator = At<char*>(self, field::kSlopeCells);
    At<uint32_t>(iterator, 0x4) = 0;
    At<uint32_t>(iterator, 0x8) = 0;
    iterator[0] = 0;
    if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
        At<int32_t>(iterator, 0x1C) < 0)
        iterator[0] = 2;
    At<int32_t>(self, field::kState) = 5;
    EndDeadline(deadline);
}

// State 5: the slope grid, each cell from the terrain's slope over its
// tiles (Service43D68's slot 36, its slot 29).
void MeasureSlopes(void* self, uint32_t budget)
{
    Deadline deadline;
    StartDeadline(deadline, budget, 4, addr::LandValueSimulator_Step5);
    void* const slope = At<void*>(self, field::kSlope);
    float* const* const rows = Rows<float>(slope);
    const uint32_t shift = Virtual<uint32_t>(slope, 0x2C) & 0xFF;
    void* const terrain = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
    int32_t x, z;
    while (InBudget(deadline)) {
        if (!NextCell(At<void*>(self, field::kSlopeCells), &x, &z)) {
            RestartCells(self);
            At<int32_t>(self, field::kState) = 4;
            break;
        }
        int32_t rect[4] = {x << shift, z << shift, ((x + 1) << shift) - 1, ((z + 1) << shift) - 1};
        rows[3 * x][z] = static_cast<float>(Virtual<double>(terrain, 0x74, rect, 1u));
    }
    EndDeadline(deadline);
}

// State 4: the intrinsic grid: the work grid plus the altitude's value
// (only with an altitude curve; without one the grid stays as it was).
void AddAltitudes(void* self, uint32_t budget)
{
    Deadline deadline;
    StartDeadline(deadline, budget, 8, addr::LandValueSimulator_Step4);
    uint8_t* const* const work = Rows<uint8_t>(At<void*>(self, field::kWork));
    uint8_t* const* const intrinsic = Rows<uint8_t>(At<void*>(self, field::kIntrinsic));
    const auto value = reinterpret_cast<AltitudeValueFn>(addr::cSC4LandValueSimulator_AltitudeValue);
    int32_t x, z;
    while (InBudget(deadline)) {
        if (!At<void*>(self, field::kAltitudeCurve) || !NextCell(At<void*>(self, field::kCells), &x, &z)) {
            RestartCells(self);
            At<int32_t>(self, field::kState) = 6;
            break;
        }
        const float altitude = CellAltitude(self, x, z);
        uint32_t sum = work[3 * x][z] + static_cast<uint32_t>(value(self, nullptr, altitude));
        if (sum > 0xFF)
            sum = 0xFF;
        intrinsic[3 * x][z] = static_cast<uint8_t>(sum);
    }
    EndDeadline(deadline);
}

using AssignFn = void(__thiscall*)(void* to, const void* from);

// State 6: the work grid starts from the intrinsic one; the influences from
// the first.
void StartInfluences(void* self)
{
    const void* const intrinsic = Virtual<void*>(At<void*>(self, field::kIntrinsic), 0x60);
    reinterpret_cast<AssignFn>(addr::SC4Vector12_Assign)(Virtual<void*>(At<void*>(self, field::kWork), 0x60), intrinsic);
    At<void*>(self, field::kInfluence) = At<void**>(self, field::kInfluences)[2];
}

// State 7: one influence per call (the key's slot 13 grid by its factor and
// the influence curve); after the last, the total grid is the work grid.
void ApplyNextInfluence(void* self)
{
    using NextFn = void(__thiscall*)(void* iterator);
    char* const node = At<char*>(self, field::kInfluence);
    if (node != At<char*>(self, field::kInfluences)) {
        const InfluenceContext context{At<float>(node, 0x14), At<void*>(self, field::kInfluenceCurve)};
        void* const source = Virtual<void*>(At<void*>(node, 0x10), 0x34);
        reinterpret_cast<ApplyInfluenceFn>(addr::cSC4LandValueSimulator_ApplyInfluence)(
            At<void*>(self, field::kWork), source,
            reinterpret_cast<AddInfluenceFn>(addr::cSC4LandValueSimulator_AddInfluence), &context);
        reinterpret_cast<NextFn>(addr::SC4MapIterator_Next)(&At<void*>(self, field::kInfluence));
        return;
    }
    const void* const work = Virtual<void*>(At<void*>(self, field::kWork), 0x60);
    reinterpret_cast<AssignFn>(addr::SC4Vector12_Assign)(Virtual<void*>(At<void*>(self, field::kTotal), 0x60), work);
    RestartCells(self);
    ClearSums(self);
    At<int32_t>(self, field::kState) = 8;
}

// State 8: the average land value over the cells counted in state 3; the
// monthly figure 0x6A663AC8, then state 9 and message 0x87CC75B3.
void Average(void* self, uint32_t budget)
{
    Deadline deadline;
    StartDeadline(deadline, budget, 0x10, addr::LandValueSimulator_Step8);
    uint8_t* const* const total = Rows<uint8_t>(At<void*>(self, field::kTotal));
    int32_t x, z;
    for (;;) {
        if (!InBudget(deadline)) {
            EndDeadline(deadline);
            return;
        }
        if (!NextCell(At<void*>(self, field::kCells), &x, &z))
            break;
        if ((At<uint8_t>(self, field::kEveryCell) || HasLot(self, x, z)) && HasLand(self, x, z)) {
            At<int32_t>(self, field::kSum) += total[3 * x][z];
            ++At<int32_t>(self, field::kCount);
        }
    }
    const int32_t count = At<int32_t>(self, field::kCount);
    const uint8_t average = count < 1 ? 0 : static_cast<uint8_t>(At<int32_t>(self, field::kSum) / count);
    At<uint8_t>(self, field::kAverage) = average;
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, 0x6A663AC8u, static_cast<uint32_t>(average));
    At<int32_t>(self, field::kState) = 9;
    reinterpret_cast<ThisFn>(addr::cSC4LandValueSimulator_PostUpdated)(self, nullptr);
    EndDeadline(deadline);
}

void __fastcall Simulate(void* self, void*, uint32_t budget)
{
    static bool logged = false;
    FirstCall("Simulate", logged);
    using StopwatchFn = void(__thiscall*)(void* stopwatch, uint32_t mode);
    using StartFn = void(__thiscall*)(void* stopwatch);
    uint32_t stopwatch[6] = {}; // started and never read, as the game's; it reads the clock
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Construct)(stopwatch, 4);
    reinterpret_cast<StartFn>(addr::SC4Stopwatch_Start)(stopwatch);
    switch (At<int32_t>(self, field::kState)) {
    case 0:
        RestartCells(self);
        At<int32_t>(self, field::kState) = 1;
        break;
    case 1: MarkLand(self, budget); break;
    case 2: SpreadWater(self); break;
    case 3: SurveyAltitudes(self, budget); break;
    case 4: AddAltitudes(self, budget); break;
    case 5: MeasureSlopes(self, budget); break;
    case 6:
        StartInfluences(self);
        At<int32_t>(self, field::kState) = 7;
        break;
    case 7: ApplyNextInfluence(self); break;
    case 8: Average(self, budget); break;
    default: break;
    }
}

using StepFn = void(__fastcall*)(void*, void*, uint32_t);
StepFn g_originalSimulate = nullptr;

bool InstallStep(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4LandValueSimulator_Simulate);
    return game::IsBuild641() && entry && hook::Install(*entry, &Simulate, &g_originalSimulate);
}

const bool g_registeredStep = registry::Register({
    "simulators.landvalue.step",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_Simulate,
    "Our land value step: land, waterfront, altitudes, slopes and influences in nine states (docs/landvalue.md)",
    false,
    InstallStep,
});

// --- The interface at +0x4: cIGZMessageTarget2 (vtable 0x00AB1660) ----------------

// 3 (0x006BF0D0) DoMessage: the step, the month's restart, changes.
uint8_t __fastcall DoMessage(void* interface4, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(interface4) - 0x4;
    using StepFn2 = void(__thiscall*)(void* self, uint32_t budget);
    const auto step = reinterpret_cast<StepFn2>(addr::cSC4LandValueSimulator_Simulate);
    switch (Virtual<uint32_t>(message, 0x10)) { // GetType
    case 0x66956814: step(self, 5); break;
    case 0x6695681A: step(self, Virtual<uint32_t>(message, 0x4C)); break;
    case 0x66956816:
        if (At<uint8_t>(self, field::kDirty) && At<int32_t>(self, field::kState) == 9) {
            At<uint8_t>(self, field::kDirty) = 0;
            At<int32_t>(self, field::kState) = 0;
        }
        break;
    case 0x99EF1142:
    case 0x99EF1143:
    case 0x678E6E3A:
    case 0x278E6003:
    case 0x07699455: At<uint8_t>(self, field::kDirty) = 1; break;
    default: break;
    }
    return 1;
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4LandValueSimulator_vftable_4);
    if (vtable[3] != 0x006BF0D0) {
        log::Write("landvalue messages: +0x4 slot 3 holds %08X, expected 006BF0D0; not installing",
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
    "simulators.landvalue.messages",
    "simulators/landvalue",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_vftable_4,
    "Our land value DoMessage (docs/landvalue.md)",
    false,
    InstallMessages,
});

// --- The tuning (0x006BE110, from Init) -------------------------------------------

// A curve made for the tuning (0x10 bytes, 0x007D0410 zeroes them).
void* LoadCurve(void* self, std::size_t offset, void* property)
{
    using LoadCurveFromPropertyFn = uint8_t(__thiscall*)(void* curve, void* property);
    if (!At<void*>(self, offset)) {
        void* const curve = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0x10);
        if (curve)
            std::memset(curve, 0, 0x10);
        At<void*>(self, offset) = curve;
    }
    reinterpret_cast<LoadCurveFromPropertyFn>(addr::SC4Curve_LoadFromProperty)(At<void*>(self, offset), property);
    return At<void*>(self, offset);
}

void DropCurve(void* self, std::size_t offset)
{
    using FreeFn2 = void(__cdecl*)(void* p);
    if (void* const curve = At<void*>(self, offset)) {
        if (void* const points = At<void*>(curve, 0))
            Free(points);
        reinterpret_cast<FreeFn2>(addr::SC4_operator_delete)(curve);
        At<void*>(self, offset) = nullptr;
    }
}

// The values of a property (its value, slot 5) as bytes, words or floats
// (variant slots 95, 103 and 111).
void* PropertyValue(void* holder, uint32_t id)
{
    return Virtual<void*>(Virtual<void*>(holder, 0x24, id), 0x14);
}

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    using GetPropertyUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
    using IndexFn = uint32_t*(__thiscall*)(void* map, const uint32_t* key);
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xE7E2C8D8};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    reinterpret_cast<GetPropertyUint8Fn>(addr::SC4_GetPropertyUint8)(holder, 0x47E2C200,
                                                                   &At<uint8_t>(self, field::kCellShift));
    for (const auto& [id, offset] : {std::pair<uint32_t, std::size_t>{0x47E2C300, field::kLandValue},
                                     {0x47E2C301, field::kLowBelow},
                                     {0x47E2C310, field::kWaterBonus}}) {
        const auto* const bytes = Virtual<const uint8_t*>(PropertyValue(holder, id), 0x17C);
        At<uint8_t>(self, offset) = bytes[0];
        At<uint8_t>(self, offset + 1) = bytes[1];
    }
    if (void* const property = Virtual<void*>(holder, 0x24, 0x47E2C320u))
        LoadCurve(self, field::kAltitudeCurve, property);
    else
        DropCurve(self, field::kAltitudeCurve);
    void* const ids = PropertyValue(holder, 0x47E2C330);
    const auto* const idValues = Virtual<const uint32_t*>(ids, 0x19C);
    const auto* const factors = Virtual<const uint32_t*>(PropertyValue(holder, 0x47E2C331), 0x1BC);
    for (uint32_t i = 0; i < Virtual<uint32_t>(ids, 0x10); ++i) {
        const uint32_t source = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D68), 0x80, idValues[i]);
        *reinterpret_cast<IndexFn>(addr::SC4UintFloatMap_Index)(static_cast<char*>(self) + field::kInfluences,
                                                               &source) = factors[i];
    }
    if (void* const property = Virtual<void*>(holder, 0x24, 0x47E2C332u))
        LoadCurve(self, field::kInfluenceCurve, property);
    else
        DropCurve(self, field::kInfluenceCurve);
    Virtual<uint32_t>(exemplar, 0x8);
}

using LoadTuningFn = void(__fastcall*)(void*, void*);
LoadTuningFn g_originalTuning = nullptr;

bool InstallTuning(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4LandValueSimulator_LoadTuning);
    return game::IsBuild641() && entry && hook::Install(*entry, &LoadTuning, &g_originalTuning);
}

const bool g_registeredTuning = registry::Register({
    "simulators.landvalue.tuning",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_LoadTuning,
    "Our land value tuning: the exemplar's values, curves and influences (docs/landvalue.md)",
    false,
    InstallTuning,
});

// --- Init (slot 3, 0x006BE3B0) and Shutdown (slot 4, 0x006BDF70) -------------------

void Reference(void* self, std::size_t offset, void* object)
{
    if (At<void*>(self, offset))
        return;
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4); // AddRef
}

// A cSC4SimGridBool8 or cSC4SimGridFloat32 as Init constructs it.
void NewGrid(void* self, std::size_t offset, std::uintptr_t vtable, uint32_t vtable4, uint32_t iid, uint32_t cellShift,
             uint32_t id)
{
    if (At<void*>(self, offset))
        return;
    auto* const grid = static_cast<uint32_t*>(reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0x44));
    if (grid) {
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
    }
    At<void*>(self, offset) = grid;
    Virtual<uint32_t>(grid, 0x4);
    Virtual<uint32_t>(grid, 0xC);
    Virtual<uint32_t>(grid, 0x24, cellShift);
    Virtual<uint32_t>(grid, 0x18, id);
}

// A cell iterator (made when missing) over a grid's cells.
void CoverGrid(void* self, std::size_t offset, void* grid)
{
    using IteratorFn = void*(__thiscall*)(void* iterator);
    if (!At<void*>(self, offset)) {
        void* const memory = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0x20);
        At<void*>(self, offset) = memory ? reinterpret_cast<IteratorFn>(addr::SC4CellIterator_Construct)(memory) : nullptr;
    }
    const int32_t lastZ = Virtual<int32_t>(grid, 0x34) - 1;
    const int32_t lastX = Virtual<int32_t>(grid, 0x30) - 1;
    char* const iterator = At<char*>(self, offset);
    At<int32_t>(iterator, 0x10) = 0;
    At<int32_t>(iterator, 0x14) = 0;
    At<int32_t>(iterator, 0x18) = lastX;
    At<int32_t>(iterator, 0x1C) = lastZ;
    if (At<int32_t>(iterator, 0x18) < At<int32_t>(iterator, 0x10))
        At<int32_t>(iterator, 0x18) = At<int32_t>(iterator, 0x10);
    if (At<int32_t>(iterator, 0x1C) < At<int32_t>(iterator, 0x14))
        At<int32_t>(iterator, 0x1C) = At<int32_t>(iterator, 0x14);
}

constexpr uint32_t kMessages[] = {0x99EF1142, 0x99EF1143, 0x678E6E3A, 0x278E6003, 0x07699455};

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const uint32_t* value);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    Global<uint8_t>(addr::LandValueSimulator_Started) = 1;
    Reference(self, field::kCity, Global<void*>(addr::SC4_City));
    Reference(self, field::kOccupied, Global<void*>(addr::SC4_Service43D14));
    Reference(self, field::kTerrain, Global<void*>(addr::SC4_Terrain));
    reinterpret_cast<LoadTuningFn>(addr::cSC4LandValueSimulator_LoadTuning)(self, nullptr);
    const uint32_t cellShift = At<uint8_t>(self, field::kCellShift);
    NewGrid(self, field::kIntrinsic, addr::SC4SimGridBool8_Vtable, 0x00A878A4, 0x49B9E602, cellShift, 0x89D5BB39);
    NewGrid(self, field::kTotal, addr::SC4SimGridBool8_Vtable, 0x00A878A4, 0x49B9E602, cellShift, 0x89D5BB51);
    NewGrid(self, field::kWork, addr::SC4SimGridBool8_Vtable, 0x00A878A4, 0x49B9E602, cellShift, 0x89D5BB60);
    NewGrid(self, field::kSlope, addr::SC4SimGridFloat32_Vtable, 0x00A87C04, 0x49B9E60A, 8, 0x49D5BB6F);
    At<int32_t>(self, field::kState) = 0;
    CoverGrid(self, field::kCells, At<void*>(self, field::kIntrinsic));
    CoverGrid(self, field::kSlopeCells, At<void*>(self, field::kSlope));
    At<uint32_t>(self, field::kHistogramShift) = 5;
    const int32_t highest = Ftol(Virtual<double>(At<void*>(self, field::kTerrain), 0x60));
    const uint32_t zero = 0;
    reinterpret_cast<ResizeFn>(addr::SC4Uint32Vector_Resize)(static_cast<char*>(self) + field::kHistogram,
                                                            static_cast<uint32_t>(highest >> 5), &zero);
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x34, 0x6A663AC8u, 1u);
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const simulators = Global<void*>(addr::SC4_SimulatorService)) {
        struct {
            const void* vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } name;
        reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name,
                                                             reinterpret_cast<const char*>(addr::LandValueSimulator_Name));
        for (const uint32_t phase : {0u, 10u, 4u, 6u})
            Virtual<uint32_t>(simulators, 0x4C, target, phase, &name, 2u);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }
    if (!At<void*>(self, field::kMessageServer)) {
        Reference(self, field::kMessageServer, Global<void*>(addr::SC4_MessageServer));
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, target, message);
    }
    At<uint8_t>(self, field::kInitialised) = 1;
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
    if (!At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 0;
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(service, 0x50, target);
    if (At<void*>(self, field::kMessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, target, message);
        Drop(self, field::kMessageServer);
    }
    for (const std::size_t offset : {field::kTerrain, field::kOccupied, field::kCity})
        Drop(self, offset);
    for (const std::size_t grid : {field::kIntrinsic, field::kTotal, field::kWork, field::kSlope}) {
        if (At<void*>(self, grid)) {
            Virtual<uint32_t>(At<void*>(self, grid), 0x10);
            Virtual<uint32_t>(At<void*>(self, grid), 0x8);
            At<void*>(self, grid) = nullptr;
        }
    }
    for (const std::size_t iterator : {field::kCells, field::kSlopeCells}) {
        if (void*& p = At<void*>(self, iterator)) {
            operatorDelete(p);
            p = nullptr;
        }
    }
    DropCurve(self, field::kAltitudeCurve);
    DropCurve(self, field::kInfluenceCurve);
    return 1;
}

using LifecycleFn = uint8_t(__fastcall*)(void*, void*);
LifecycleFn g_originalInit = nullptr;
LifecycleFn g_originalShutdown = nullptr;

bool InstallInit(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4LandValueSimulator_Init);
    return game::IsBuild641() && entry && hook::Install(*entry, &Init, &g_originalInit);
}

bool InstallShutdown(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4LandValueSimulator_Shutdown);
    return game::IsBuild641() && entry && hook::Install(*entry, &Shutdown, &g_originalShutdown);
}

const bool g_registeredInit = registry::Register({
    "simulators.landvalue.init",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_Init,
    "Our land value Init: references, tuning, grids, cell iterators, histogram and registrations (docs/landvalue.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.landvalue.shutdown",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_Shutdown,
    "Our land value Shutdown (docs/landvalue.md)",
    false,
    InstallShutdown,
});

// --- Saving: cIGZSerializable (vtable 0x00AB1648) at +0x8 ---------------------------

constexpr std::size_t kSerializable = 0x8;

using WriteObjectFn = uint8_t(__cdecl*)(void* stream, const void* object);
using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t interfaceId, void* out, uint32_t flags);

// Version 1: the intrinsic, total and slope grids, whether the step has work
// left, the average land value and the average altitude.
uint8_t __fastcall Write(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    const auto writeReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    writeReference(stream, At<void*>(self, field::kIntrinsic));
    writeReference(stream, At<void*>(self, field::kTotal));
    writeReference(stream, At<void*>(self, field::kSlope));
    const bool pending = At<uint8_t>(self, field::kDirty) || At<int32_t>(self, field::kState) != 9;
    Virtual<uint8_t>(stream, 0x14, static_cast<uint8_t>(pending ? 1 : 0));
    Virtual<uint8_t>(stream, 0x14, At<uint8_t>(self, field::kAverage));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kAverageAltitude));
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

uint8_t __fastcall Read(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    const auto readReference = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    readReference(stream, 0xC9BD939A, self + field::kIntrinsic, 0);
    readReference(stream, 0xC9BD939A, self + field::kTotal, 0);
    readReference(stream, 0xC9BD939A, self + field::kSlope, 0);
    uint8_t pending = 0;
    Virtual<uint8_t>(stream, 0x14, &pending);
    At<uint8_t>(self, field::kDirty) = pending != 0 ? 1 : 0;
    Virtual<uint8_t>(stream, 0x14, self + field::kAverage);
    Virtual<uint8_t>(stream, 0x30, self + field::kAverageAltitude);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

using SaveFn = uint8_t(__fastcall*)(void*, void*, void*);
SaveFn g_originalWrite = nullptr;
SaveFn g_originalRead = nullptr;

bool InstallSave(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4LandValueSimulator_Write);
    return game::IsBuild641() && entry && hook::Install(*entry, &Write, &g_originalWrite);
}

bool InstallLoad(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4LandValueSimulator_Read);
    return game::IsBuild641() && entry && hook::Install(*entry, &Read, &g_originalRead);
}

const bool g_registeredSave = registry::Register({
    "simulators.landvalue.save",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_Write,
    "Our land value Write: the save record, version 1 (docs/landvalue.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.landvalue.load",
    "simulators/landvalue",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LandValueSimulator_Read,
    "Our land value Read: the save record, version 1 (docs/landvalue.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::landvalue_sim
