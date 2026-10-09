// Our cSC4AuraSimulator (build 641), first part: the interface, the
// monthly pass and its helpers, the occupant handler and DoMessage, each a
// replacement on the original object (docs/aura.md).
#include "core/address_check.h"
#include "core/aura_sim.h"
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

namespace opensc4::aura_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("aura: first %s", name);
}

namespace {

using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using SpreadFn = void(__cdecl*)(void* rows, int32_t tractsX, int32_t tractsZ, int32_t tractX, int32_t tractZ,
                                float radius, float value, float edge);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto SpreadAround16 = reinterpret_cast<SpreadFn>(addr::SC4SimGrid_SpreadAround);
const auto SpreadAround8 = reinterpret_cast<SpreadFn>(addr::SC4SimGrid_SpreadAround8);

void* Service(std::uintptr_t global) { return Global<void*>(global); }

// 0x0068BFE0: the rating offset (+0x88), value 0xAA5B8407 of service
// 0x00B43D90 (its slot 24).
void UpdateRatingOffset(void* self)
{
    At<int32_t>(self, field::kRatingOffset) = Virtual<int32_t>(Service(addr::SC4_Service43D90), 0x60, 0xAA5B8407u, 0u);
}

// The game's functions, called through their addresses so that each is the
// game's or ours as configured.
template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

// --- The interface (cISC4AuraSimulator slots 5-15) ----------------------------

// 5 (0x0068BAB0): the aura grid's value (its slot 7).
uint32_t __fastcall GetAuraValue(void* self, void*, int32_t x, int32_t z)
{
    return Virtual<uint32_t>(At<void*>(self, field::kAuraGrid), 0x1C, x, z);
}

// 6 (0x0068BAD0): a transient effect of a kind from the tuning (radius
// +0x128, value +0x118), through slot 7, unless either is zero.
void __fastcall AddTransientEffectOfKind(void* self, void*, float x, float z, uint32_t kind)
{
    const float radius = At<float>(self, field::kTransientRadius + 4 * kind);
    const float value = At<float>(self, field::kTransientValue + 4 * kind);
    if (!(radius == 0.0f) && !(value == 0.0f))
        Virtual<void>(self, 0x1C, x, z, radius, value);
}

// 7 (0x0068BB30): spread into the transient grid around the position's
// tract, the radius in tracts (the grid's slot 14 is the tract size).
void __fastcall AddTransientEffect(void* self, void*, float x, float z, float radius, float value)
{
    void* const grid = At<void*>(self, field::kTransientGrid);
    const float tracts = static_cast<float>(radius / Virtual<double>(grid, 0x38));
    int32_t tractX, tractZ;
    Virtual<uint32_t>(grid, 0x4C, x, z, &tractX, &tractZ);
    const int32_t countZ = Virtual<int32_t>(grid, 0x34);
    const int32_t countX = Virtual<int32_t>(grid, 0x30);
    SpreadAround8(Virtual<void*>(grid, 0x60), countX, countZ, tractX, tractZ, tracts, value, 0.0f);
}

// 8 (0x0068BAC0).
uint8_t __fastcall GetMayorRating(void* self, void*)
{
    return At<uint8_t>(self, field::kMayorRating);
}

// 9, 10 (0x0068D1E0, 0x0068D200): the best or worst tract and its value.
template <std::size_t X>
uint8_t __fastcall GetExtremeRating(void* self, void*, int32_t* location)
{
    location[0] = At<int32_t>(self, X);
    location[1] = At<int32_t>(self, X + 4);
    return At<uint8_t>(self, X + 8);
}

// 11 (0x0068C0D0): strike busting, through the services that run strikes.
void __fastcall BustStrike(void*, void*, uint32_t type)
{
    if (type == 1 || type == 0)
        if (void* const police = Service(addr::SC4_PoliceSimulator))
            Virtual<void>(police, 0x34);
    if (type == 2 || type == 0)
        if (void* const fire = Service(addr::SC4_Service43D78))
            Virtual<void>(fire, 0x38);
    if (type == 3 || type == 0)
        if (void* const civic = Service(addr::SC4_Service43DA4))
            Virtual<void>(civic, 0x24);
    if (type == 4 || type == 0)
        if (void* const civic = Service(addr::SC4_Service43DA4))
            Virtual<void>(civic, 0x20);
}

// 12-15: one field each.
template <std::size_t Offset>
void* __fastcall FieldGetter(void* self, void*)
{
    return At<void*>(self, Offset);
}

// --- Grid walks of the step ---------------------------------------------------
//
// Each calls `apply(cell, value)` for every cell of `dst` with the value of
// `src` there: the same tract size one to one; a finer `src` averaged over
// the block (sum >> shift >> shift); a coarser one repeated over the block.
// Grids' slots: 11 the size shift, 12 and 13 the tract counts, 24 and 25 the
// rows (as 0x006BDA30, 0x006F8970, 0x0068C950 and 0x0068C270 do).

template <typename D, typename S, typename Apply>
void Walk(void* dst, void* src, Apply apply)
{
    const int32_t dstShift = Virtual<int32_t>(dst, 0x2C);
    const int32_t dstX = Virtual<int32_t>(dst, 0x30);
    const int32_t dstZ = Virtual<int32_t>(dst, 0x34);
    D* const* const dstRows = Rows<D>(dst);
    const int32_t srcShift = Virtual<int32_t>(src, 0x2C);
    const int32_t srcX = Virtual<int32_t>(src, 0x30);
    const int32_t srcZ = Virtual<int32_t>(src, 0x34);
    S* const* const srcRows = Rows<S>(src, 0x64);
    if (dstShift == srcShift) {
        for (int32_t x = 0; x < dstX; ++x)
            for (int32_t z = 0; z < dstZ; ++z)
                apply(&dstRows[3 * x][z], static_cast<int32_t>(srcRows[3 * x][z]));
    } else if (srcShift < dstShift) {
        const int32_t shift = (dstShift - srcShift) & 0x1F;
        for (int32_t x = 0; x < dstX; ++x)
            for (int32_t z = 0; z < dstZ; ++z) {
                int32_t sum = 0;
                for (int32_t sx = x << shift; sx < (x + 1) << shift; ++sx)
                    for (int32_t sz = z << shift; sz < (z + 1) << shift; ++sz)
                        sum += srcRows[3 * sx][sz];
                apply(&dstRows[3 * x][z], (sum >> shift) >> shift);
            }
    } else {
        const int32_t shift = (srcShift - dstShift) & 0x1F;
        for (int32_t sx = 0; sx < srcX; ++sx)
            for (int32_t sz = 0; sz < srcZ; ++sz) {
                const int32_t value = srcRows[3 * sx][sz];
                for (int32_t x = sx << shift; x < (sx + 1) << shift; ++x)
                    for (int32_t z = sz << shift; z < (sz + 1) << shift; ++z)
                        apply(&dstRows[3 * x][z], value);
            }
    }
}

// Whether the lot on a cell is zoned (its slot 31 is 1, 2 or 3).
bool ZonedLot(int32_t x, int32_t z)
{
    void* const lot = Virtual<void*>(Service(addr::SC4_LotManager), 0x18, x, z, 0);
    if (!lot)
        return false;
    const int32_t zone = Virtual<int32_t>(lot, 0x7C);
    return zone == 1 || zone == 2 || zone == 3;
}

// The same into the cell-size effect grid, only for cells with a zoned lot,
// from a grid at cell size or coarser (0x0068C550 for 4-byte cells,
// 0x0068C750 for bytes). `S` is the raw cell type.
template <typename S, typename Apply>
void WalkZoned(void* dst, void* src, Apply apply)
{
    const int32_t dstX = Virtual<int32_t>(dst, 0x30);
    const int32_t dstZ = Virtual<int32_t>(dst, 0x34);
    int8_t* const* const dstRows = Rows<int8_t>(dst);
    const int32_t srcShift = Virtual<int32_t>(src, 0x2C);
    const int32_t srcX = Virtual<int32_t>(src, 0x30);
    const int32_t srcZ = Virtual<int32_t>(src, 0x34);
    S* const* const srcRows = Rows<S>(src, 0x64);
    if (srcShift == 0) {
        for (int32_t x = 0; x < dstX; ++x)
            for (int32_t z = 0; z < dstZ; ++z)
                if (ZonedLot(x, z))
                    apply(&dstRows[3 * x][z], srcRows[3 * x][z]);
    } else {
        const int32_t shift = srcShift & 0x1F;
        for (int32_t sx = 0; sx < srcX; ++sx)
            for (int32_t sz = 0; sz < srcZ; ++sz) {
                const S value = srcRows[3 * sx][sz];
                for (int32_t x = sx << shift; x < (sx + 1) << shift; ++x)
                    for (int32_t z = sz << shift; z < (sz + 1) << shift; ++z)
                        if (ZonedLot(x, z))
                            apply(&dstRows[3 * x][z], value);
            }
    }
}

// The effect callbacks (0x0068BDA0, 0x0068BE70, 0x0068BEE0, 0x0068BBB0,
// 0x0068BE10): a cell that is -128 stays so; otherwise the curve at the
// value (or the value itself without a curve) is added, truncated and
// clamped.
void AddThroughCurve(int8_t* cell, float x, const void* curve)
{
    if (*cell == -128)
        return;
    *cell = ClampAura(Ftol(static_cast<double>(*cell) + EvaluateCurve(curve, x)));
}

void AddInteger(int8_t* cell, int32_t value, const void* curve)
{
    if (*cell == -128)
        return;
    if (curve)
        AddThroughCurve(cell, static_cast<float>(value), curve);
    else
        *cell = ClampAura(*cell + value);
}

// --- The step's states ----------------------------------------------------------

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

constexpr std::uintptr_t kLotPassStep = 0x00B47B60; // the adaptive step and its counter
constexpr std::uintptr_t kLotPassOnce = 0x00B47B68;

// 2: the park map, the park sources times the park department's funding
// (budget department 0x8A4260A6, its slot 10), truncated.
void ScaleParkMap(void* self)
{
    void* const department = Virtual<void*>(Service(addr::SC4_BudgetSimulator), 0x70, 0x8A4260A6u);
    const float funding = *Virtual<const float*>(department, 0x28);
    Walk<int16_t, int16_t>(At<void*>(self, field::kParkMap), At<void*>(self, field::kParkSources),
                           [funding](int16_t* cell, int32_t value) {
                               const auto v = static_cast<int16_t>(value);
                               *cell = static_cast<int16_t>(
                                   v > 0 ? Ftol(static_cast<double>(v) * funding) : Ftol(static_cast<double>(v)));
                           });
}

// 3, one department grid per call: the first sets +0x6C, the others add to
// it, each times its department's funding (1 without one); after the last,
// +0x68 becomes a copy of +0x6C.
void SumDepartmentGrids(void* self)
{
    using FindFn = void(__thiscall*)(void* map, void** out, const uint32_t* key);
    using NextFn = void**(__thiscall*)(void** iterator);
    using AssignFn = void*(__thiscall*)(void* dst, const void* src);
    if (At<uint32_t>(self, field::kDepartmentGridCount) == 0) {
        At<uint32_t>(self, field::kState) = 4;
        return;
    }
    uint32_t& key = At<uint32_t>(self, field::kDepartmentKey);
    void* const header = At<void*>(self, field::kDepartmentGrids);
    const bool first = key == 0xFFFFFFFFu;
    void* node;
    if (first) {
        node = At<void*>(header, 0x8);
    } else {
        reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(static_cast<char*>(self) + field::kDepartmentGrids, &node,
                                                          &key);
        bool done = node == header;
        if (!done) {
            done = *reinterpret_cast<NextFn>(addr::SC4MapIterator_Next)(&node) == header;
            if (done) {
                void* const rows = Virtual<void*>(At<void*>(self, field::kDepartmentSum), 0x60);
                void* const target = Virtual<void*>(At<void*>(self, field::kLandmarkMap), 0x60);
                reinterpret_cast<AssignFn>(addr::cSC4AuraSimulator_AssignRows)(target, rows);
            }
        }
        if (done) {
            key = 0xFFFFFFFFu;
            At<uint32_t>(self, field::kState) = 4;
            return;
        }
    }
    void* const grid = At<void*>(node, 0x14);
    key = At<uint32_t>(node, 0x10);
    float funding = 1.0f;
    if (key) {
        if (void* const department = Virtual<void*>(Service(addr::SC4_BudgetSimulator), 0x70, key))
            funding = *Virtual<const float*>(department, 0x28);
    }
    void* const sum = At<void*>(self, field::kDepartmentSum);
    if (first)
        Walk<int16_t, int16_t>(sum, grid, [funding](int16_t* cell, int32_t value) {
            const auto v = static_cast<int16_t>(value);
            *cell = static_cast<int16_t>(v > 0 ? Ftol(static_cast<double>(v) * funding) : Ftol(static_cast<double>(v)));
        });
    else
        Walk<int16_t, int16_t>(sum, grid, [funding](int16_t* cell, int32_t value) {
            const auto v = static_cast<int16_t>(value);
            const double scaled = v > 0 ? static_cast<double>(v) * funding : static_cast<double>(v);
            *cell = static_cast<int16_t>(Ftol(static_cast<double>(*cell) + scaled));
        });
}

// 4: the effect grid cleared to -128, the lot iterator made or rewound;
// with lots, the effect values updated and on to 5, else the pass ends.
void StartLotPass(void* self)
{
    Virtual<void>(At<void*>(self, field::kEffectGrid), 0x6C, static_cast<int32_t>(-128));
    void* const lots = Service(addr::SC4_LotManager);
    void**& iterator = At<void**>(self, field::kLotIterator);
    if (!iterator)
        iterator = Virtual<void**>(lots, 0x3C);
    else if (iterator[1])
        iterator[0] = **static_cast<void***>(iterator[1]);
    if (Virtual<int32_t>(lots, 0x40)) {
        CallThis<void>(addr::cSC4AuraSimulator_UpdateEffectValues, self);
        At<uint32_t>(self, field::kState) = 5;
    } else {
        At<uint32_t>(self, field::kState) = 0;
    }
}

// 5: each lot's aura (0x0068CE00) over its rectangle in the effect grid,
// keeping the higher; cut short when the time budget runs out.
void PassLots(void* self, uint32_t budget)
{
    using StartFn = void(__thiscall*)(Deadline* deadline, uint32_t budget);
    using CheckFn = uint8_t(__thiscall*)(Deadline* deadline);
    using EndFn = void(__thiscall*)(Deadline* deadline);
    void* const grid = At<void*>(self, field::kEffectGrid);
    const int32_t shift = Virtual<int32_t>(grid, 0x2C) & 0x1F;
    int8_t* const* const rows = Rows<int8_t>(grid);
    uint32_t& once = Global<uint32_t>(kLotPassOnce);
    if (!(once & 1)) {
        once |= 1;
        Global<uint32_t>(kLotPassStep) = 0x400;
        Global<uint32_t>(kLotPassStep + 4) = 0;
    }
    Deadline deadline{};
    reinterpret_cast<StartFn>(addr::SC4Deadline_Start)(&deadline, budget);
    deadline.count = 0;
    deadline.stepGlobal = &Global<uint32_t>(kLotPassStep);
    deadline.step = *deadline.stepGlobal;
    deadline.timedOut = 0;
    for (;;) {
        deadline.count += deadline.step;
        if (!(deadline.count & 0x3FF) && reinterpret_cast<CheckFn>(addr::SC4Deadline_Expired)(&deadline)) {
            deadline.timedOut = 1;
            break;
        }
        void** const iterator = At<void**>(self, field::kLotIterator);
        void** const list = static_cast<void**>(iterator[1]);
        void** const current = static_cast<void**>(iterator[0]);
        if (!list || current == *list) {
            At<uint32_t>(self, field::kState) = 6;
            break;
        }
        iterator[0] = current[0];
        void* const lot = current[2];
        const int8_t aura = CallThis<int8_t>(addr::cSC4AuraSimulator_OccupantAura, self, lot);
        int32_t rect[4];
        Virtual<void>(lot, 0x38, rect);
        const int32_t x1 = rect[0] >> shift, z1 = rect[1] >> shift, x2 = rect[2] >> shift, z2 = rect[3] >> shift;
        for (int32_t x = x1; x <= x2; ++x)
            for (int32_t z = z1; z <= z2; ++z)
                if (aura > rows[3 * x][z])
                    rows[3 * x][z] = aura;
    }
    reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
}

// 7: each pollution grid (the pollution simulator's slot 5) through its curve.
void AddPollution(void* self)
{
    void* const pollution = Service(addr::SC4_PollutionSimulator);
    for (uint32_t i = 0;; ++i) {
        void** const curves = At<void**>(self, field::kPollutionCurves);
        const auto count = static_cast<uint32_t>((At<char*>(self, field::kPollutionCurves + 4) -
                                                  reinterpret_cast<char*>(curves)) >> 2);
        if (i >= count)
            break;
        if (!curves[i])
            continue;
        void* const grid = Virtual<void*>(pollution, 0x14, i);
        const void* const curve = At<void**>(self, field::kPollutionCurves)[i];
        Walk<int8_t, int16_t>(At<void*>(self, field::kEffectGrid), grid, [curve](int8_t* cell, int32_t value) {
            AddInteger(cell, static_cast<int16_t>(value), curve);
        });
    }
}

// 14: the smoothed grid: the effect grid averaged over each of its tracts
// plus the rating offset, moved toward that by (1 - +0x104) of the
// difference, rounded away from zero; -128 where no cell had a value.
void Smooth(void* self)
{
    void* const effects = At<void*>(self, field::kEffectGrid);
    void* const smoothed = At<void*>(self, field::kSmoothedGrid);
    const int8_t* const* const from = Rows<const int8_t>(effects);
    int8_t* const* const to = Rows<int8_t>(smoothed);
    const int32_t countX = Virtual<int32_t>(smoothed, 0x30);
    const int32_t countZ = Virtual<int32_t>(smoothed, 0x34);
    UpdateRatingOffset(self);
    const float rate = static_cast<float>(1.0f - At<float>(self, field::kSmoothing));
    const int32_t shift = (Virtual<int32_t>(smoothed, 0x2C) - Virtual<int32_t>(effects, 0x2C)) & 0x1F;
    for (int32_t x = 0; x < countX; ++x)
        for (int32_t z = 0; z < countZ; ++z) {
            int32_t sum = 0, count = 0;
            for (int32_t sx = x << shift; sx < (x + 1) << shift; ++sx)
                for (int32_t sz = z << shift; sz < (z + 1) << shift; ++sz) {
                    const int8_t value = from[3 * sx][sz];
                    if (value != -128) {
                        ++count;
                        sum += value;
                    }
                }
            int8_t& cell = to[3 * x][z];
            if (count == 0) {
                cell = -128;
                continue;
            }
            int32_t result = sum / count + At<int32_t>(self, field::kRatingOffset);
            if (cell != -128) {
                const int32_t old = cell;
                const int32_t difference = result - old;
                if (difference != 0) {
                    // x87 at 53 bits: f * rate exact, +-0.5 then 1.5 * 2^23
                    // rounded to float (the integer in its low bits), then
                    // one less (more) when f itself is that integer.
                    const double f = static_cast<double>(difference) * rate;
                    const float magic = difference < 0 ? static_cast<float>((f - 0.5) + 12582912.0)
                                                       : static_cast<float>((0.5 + f) + 12582912.0);
                    uint32_t bits;
                    std::memcpy(&bits, &magic, sizeof bits);
                    int32_t step = static_cast<int32_t>(bits + 0xB4C00000u);
                    const int32_t nearer = difference < 0 ? step + 1 : step - 1;
                    if (f == static_cast<double>(nearer))
                        step = nearer;
                    result = step + old;
                }
            }
            cell = ClampAura(result);
        }
}

// 17: the transient grid decays (times +0x108, truncated); then the pass is
// announced and over.
void DecayTransients(void* self)
{
    void* const grid = At<void*>(self, field::kTransientGrid);
    int8_t* const* const rows = Rows<int8_t>(grid);
    const int32_t countX = Virtual<int32_t>(grid, 0x30);
    const int32_t countZ = Virtual<int32_t>(grid, 0x34);
    const float decay = At<float>(self, field::kTransientDecay);
    for (int32_t x = 0; x < countX; ++x)
        for (int32_t z = 0; z < countZ; ++z) {
            int8_t& cell = rows[3 * x][z];
            if (cell != 0)
                cell = static_cast<int8_t>(Ftol(static_cast<double>(cell) * decay));
        }
    CallThis<void>(addr::cSC4AuraSimulator_PostPassDone, self);
}

// 0x0068F190: one state per call.
void __fastcall Simulate(void* self, void*, uint32_t budget)
{
    static bool logged = false;
    FirstCall("Simulate", logged);
    uint32_t& state = At<uint32_t>(self, field::kState);
    void* const effects = At<void*>(self, field::kEffectGrid);
    switch (state) {
    case 1:
        if (!At<uint8_t>(self, field::kInitialised))
            return;
        At<uint32_t>(self, field::kPassStart) = Virtual<uint32_t>(Service(addr::SC4_SimulatorService), 0x20);
        state = 2;
        return;
    case 2:
        ScaleParkMap(self);
        state = 3;
        return;
    case 3: SumDepartmentGrids(self); return;
    case 4: StartLotPass(self); return;
    case 5: PassLots(self, budget); return;
    case 6: {
        const void* const curve = At<void*>(self, field::kParkCurve);
        if (!curve)
            return; // stays in 6, as the game's does
        Walk<int8_t, int16_t>(effects, At<void*>(self, field::kParkMap), [curve](int8_t* cell, int32_t value) {
            AddInteger(cell, static_cast<int16_t>(value), curve);
        });
        state = 7;
        return;
    }
    case 7:
        AddPollution(self);
        state = 8;
        return;
    case 8:
    case 9: {
        const void* const curve = At<void*>(self, state == 8 ? field::kCoverageCurve1 : field::kCoverageCurve2);
        if (curve) {
            void* const grid = Virtual<void*>(Service(addr::SC4_Service43DA4), state == 8 ? 0x9C : 0xA0);
            WalkZoned<float>(effects, grid, [curve](int8_t* cell, float value) {
                if (*cell == -128)
                    return;
                if (curve)
                    AddThroughCurve(cell, value, curve);
                else
                    *cell = ClampAura(Ftol(static_cast<double>(*cell) + value));
            });
        }
        state = state + 1;
        return;
    }
    case 10: {
        const void* const curve = At<void*>(self, field::kService43D70Curve);
        if (curve) {
            void* const grid = Virtual<void*>(Service(addr::SC4_Service43D70), 0x14);
            Walk<int8_t, uint8_t>(effects, grid, [curve](int8_t* cell, int32_t value) {
                AddInteger(cell, static_cast<uint8_t>(value), curve);
            });
        }
        state = 11;
        return;
    }
    case 11: {
        const void* const curve = At<void*>(self, field::kService43DA8Curve1);
        if (curve) {
            void* const grid = Virtual<void*>(Service(addr::SC4_Service43DA8), 0x34);
            WalkZoned<uint8_t>(effects, grid, [curve](int8_t* cell, uint8_t value) {
                if (*cell == -128 || !curve)
                    return;
                AddThroughCurve(cell, static_cast<float>(value ? value : 0xFF), curve);
            });
        }
        state = 12;
        return;
    }
    case 12: {
        const void* const curve = At<void*>(self, field::kService43DA8Curve2);
        if (curve) {
            void* const grid = Virtual<void*>(Service(addr::SC4_Service43DA8), 0x28);
            WalkZoned<uint8_t>(effects, grid,
                               [curve](int8_t* cell, uint8_t value) { AddInteger(cell, value, curve); });
        }
        state = 13;
        return;
    }
    case 13:
        Walk<int8_t, int8_t>(effects, At<void*>(self, field::kLandmarkSources), [](int8_t* cell, int32_t value) {
            AddInteger(cell, static_cast<int8_t>(value), nullptr);
        });
        state = 14;
        return;
    case 14:
        Smooth(self);
        state = 15;
        return;
    case 15:
        CallThis<void>(addr::cSC4AuraSimulator_UpdateMayorRating, self);
        state = 16;
        return;
    case 16:
        CallThis<void>(addr::cSC4AuraSimulator_UpdateLowClusters, self);
        CallThis<void>(addr::cSC4AuraSimulator_MaybeRiot, self);
        state = 17;
        return;
    case 17:
        DecayTransients(self);
        state = 0;
        return;
    default: return;
    }
}

using SimulateFn = void(__fastcall*)(void*, void*, uint32_t);
SimulateFn g_originalSimulate = nullptr;

bool InstallStep(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_Simulate);
    return game::IsBuild641() && entry && hook::Install(*entry, &Simulate, &g_originalSimulate);
}

const bool g_registeredStep = registry::Register({
    "simulators.aura.step",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_Simulate,
    "Our aura pass: the month's effects, smoothing and mayor rating in 17 states (docs/aura.md)",
    false,
    InstallStep,
});

// --- Helpers of the step --------------------------------------------------------

// 0x0068CE00: a lot's aura: the weighted average of its occupants' effect
// values (each occupant's map of effect ids to weights; table entries of
// departments 0-2 count), plus the height curve at the lot's height below
// the terrain; -128 with none.
int8_t __fastcall OccupantAura(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("OccupantAura", logged);
    if (!Virtual<void*>(lot, 0xC0))
        return -128;
    int32_t weights = 0, sum = 0;
    bool found = false;
    void** const range = Virtual<void**>(lot, 0xD8);
    for (char* element = static_cast<char*>(range[0]); element != range[1]; element += 12) {
        void* const header = *reinterpret_cast<void**>(element);
        for (void* node = At<void*>(header, 0x8); node != header;) {
            const uint32_t id = At<uint32_t>(node, 0x10);
            const uint16_t weight = At<uint16_t>(node, 0x14);
            int32_t value = 0;
            for (EffectEntry* entry = EffectTable(); entry->id; ++entry)
                if (entry->id == id) {
                    value = entry->value;
                    if (static_cast<uint32_t>(entry->department) <= 2)
                        found = true;
                    break;
                }
            sum += value * weight;
            weights += weight;
            // In-order successor (right subtree's leftmost, else up).
            if (void* right = At<void*>(node, 0xC)) {
                while (void* left = At<void*>(right, 0x8))
                    right = left;
                node = right;
            } else {
                void* parent = At<void*>(node, 0x4);
                while (node == At<void*>(parent, 0xC)) {
                    node = parent;
                    parent = At<void*>(parent, 0x4);
                }
                if (At<void*>(node, 0xC) != parent)
                    node = parent;
            }
        }
    }
    if (!found || weights <= 0)
        return -128;
    const int8_t average = ClampAura(sum / weights);
    const void* const curve = At<void*>(self, field::kHeightCurve);
    if (!curve)
        return average;
    int32_t rect[4];
    Virtual<void>(lot, 0x38, rect);
    const int32_t centerX = (rect[0] + rect[2]) / 2;
    const int32_t centerZ = (rect[1] + rect[3]) / 2;
    const int32_t lotHeight = Virtual<int32_t>(lot, 0xA4) - 2;
    const int32_t terrain = Virtual<int32_t>(Service(addr::SC4_Service43D88), 0x14, centerX, centerZ) - 2;
    const int32_t below = terrain - lotHeight;
    return ClampAura(Ftol(EvaluateCurve(curve, static_cast<float>(below))) + average);
}

// 0x0068C000: the table's values back to their bases, then for each of the
// 12 budget departments the funding curve (+0xFC) at its change since the
// first pass (+0x94, recorded then) added to the entries of that
// department.
void __fastcall UpdateEffectValues(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateEffectValues", logged);
    for (EffectEntry* entry = EffectTable(); entry->id; ++entry)
        entry->value = entry->base;
    const void* const curve = At<void*>(self, field::kFundingCurve);
    if (!curve)
        return;
    void* const budget = Service(addr::SC4_BudgetSimulator);
    for (int32_t i = 0; i < 12; ++i) {
        float& base = At<float>(self, field::kFundingBase + 4 * i);
        uint32_t bits;
        std::memcpy(&bits, &base, sizeof bits);
        if (bits == 0xBF800000u) {
            base = static_cast<float>(Virtual<double>(budget, 0x90, i));
            continue;
        }
        const float change = static_cast<float>(Virtual<double>(budget, 0x90, i) - base);
        if (change == 0.0f)
            continue;
        const int32_t value = Ftol(EvaluateCurve(curve, change));
        for (EffectEntry* entry = EffectTable(); entry->id; ++entry)
            if (entry->department == i)
                entry->value = entry->base + value;
    }
}

// 0x0068DA80: the aura grid: the smoothed grid plus the transient one,
// clamped (0 where the smoothed grid has none); the best and worst tracts
// (their centres in cells); tracts below +0x13C listed; the mayor rating,
// the aura weighted by the three population grids of service 0x00B43DA4,
// sent to the monthly figures (0x0A5CBF37).
void __fastcall UpdateMayorRating(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateMayorRating", logged);
    void* const auraGrid = At<void*>(self, field::kAuraGrid);
    int8_t* const* const aura = Rows<int8_t>(auraGrid);
    const int8_t* const* const smoothed = Rows<const int8_t>(At<void*>(self, field::kSmoothedGrid));
    const int8_t* const* const transient = Rows<const int8_t>(At<void*>(self, field::kTransientGrid));
    void* population[3] = {};
    Virtual<void>(Service(addr::SC4_Service43DA4), 0x64, &population[0], &population[1], &population[2]);
    const uint16_t* const* const pop0 = Rows<const uint16_t>(population[0]);
    const uint16_t* const* const pop1 = Rows<const uint16_t>(population[1]);
    const uint16_t* const* const pop2 = Rows<const uint16_t>(population[2]);
    int32_t weights = 0, weighted = 0;
    const int32_t tractSize = Virtual<int32_t>(auraGrid, 0x28);
    const int32_t countX = Virtual<int32_t>(auraGrid, 0x30);
    const int32_t countZ = Virtual<int32_t>(auraGrid, 0x34);
    int32_t*& lowEnd = At<int32_t*>(self, field::kLowTracts + 4);
    lowEnd = At<int32_t*>(self, field::kLowTracts); // cleared (0x006143E0 copies nothing)
    At<int16_t>(self, field::kBestValue) = -128;
    At<int16_t>(self, field::kWorstValue) = 127;
    for (int32_t x = 0; x < countX; ++x)
        for (int32_t z = 0; z < countZ; ++z) {
            const int8_t base = smoothed[3 * x][z];
            if (base == -128) {
                aura[3 * x][z] = 0;
                continue;
            }
            const int8_t value = ClampAura(transient[3 * x][z] + base);
            aura[3 * x][z] = value;
            const int16_t wide = value;
            const int32_t centreX = x * tractSize + tractSize / 2;
            const int32_t centreZ = z * tractSize + tractSize / 2;
            if (wide < At<int16_t>(self, field::kWorstValue)) {
                At<int32_t>(self, field::kWorstX) = centreX;
                At<int32_t>(self, field::kWorstZ) = centreZ;
                At<int16_t>(self, field::kWorstValue) = wide;
            }
            if (wide > At<int16_t>(self, field::kBestValue)) {
                At<int32_t>(self, field::kBestX) = centreX;
                At<int32_t>(self, field::kBestZ) = centreZ;
                At<int16_t>(self, field::kBestValue) = wide;
            }
            if (value < At<int32_t>(self, field::kLowThreshold)) {
                int32_t*& begin = At<int32_t*>(self, field::kLowTracts);
                int32_t*& capacity = At<int32_t*>(self, field::kLowTracts + 8);
                if (lowEnd != capacity) {
                    lowEnd[0] = x;
                    lowEnd[1] = z;
                    lowEnd[2] = 1;
                    lowEnd += 3;
                } else {
                    // Grown as the game's vector does: size + max(size, 1).
                    const uint32_t size = static_cast<uint32_t>((lowEnd - begin) / 3);
                    const uint32_t grown = size + (size < 1 ? 1 : size);
                    auto* const fresh = grown ? static_cast<int32_t*>(Allocate(grown * 12)) : nullptr;
                    int32_t* out = fresh;
                    for (int32_t* in = begin; in != lowEnd; in += 3, out += 3)
                        if (out) {
                            out[0] = in[0];
                            out[1] = in[1];
                            out[2] = in[2];
                        }
                    if (out) {
                        out[0] = x;
                        out[1] = z;
                        out[2] = 1;
                    }
                    if (begin)
                        Free(begin);
                    begin = fresh;
                    lowEnd = out + 3;
                    capacity = fresh + 3 * grown;
                }
            }
            const int32_t people = pop2[3 * x][z] + pop1[3 * x][z] + pop0[3 * x][z];
            weights += people;
            weighted += value * people;
        }
    Virtual<uint32_t>(population[0], 0x8);
    Virtual<uint32_t>(population[1], 0x8);
    Virtual<uint32_t>(population[2], 0x8);
    At<int32_t>(self, field::kMayorRating) = weights > 0 ? weighted / weights : 0;
    Virtual<void>(Service(addr::SC4_MonthlyFigures), 0x30, 0x0A5CBF37u, At<int32_t>(self, field::kMayorRating));
}

// 0x0068D4B0: the low tracts in clusters: each joins the first cluster
// within +0x138 (Manhattan distance; the centre moves to the mean) or
// starts one; clusters smaller than +0x140 are dropped.
void __fastcall UpdateLowClusters(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateLowClusters", logged);
    struct Node {
        Node* next;
        Node* prev;
        int32_t x, z, count;
    };
    const int32_t* const low = At<int32_t*>(self, field::kLowTracts);
    const int32_t lowCount = static_cast<int32_t>((At<int32_t*>(self, field::kLowTracts + 4) - low) / 3);
    Node* const head = At<Node*>(self, field::kClusters);
    for (Node* node = head->next; node != head;) {
        Node* const next = node->next;
        Free(node);
        node = next;
    }
    head->next = head;
    head->prev = head;
    for (int32_t i = 0; i < lowCount; ++i) {
        const int32_t* const tract = low + 3 * i;
        Node* match = nullptr;
        for (Node* node = head->next; node != head; node = node->next) {
            const int32_t dx = tract[0] < node->x ? node->x - tract[0] : tract[0] - node->x;
            const int32_t dz = tract[1] < node->z ? node->z - tract[1] : tract[1] - node->z;
            if (dz + dx < At<int32_t>(self, field::kClusterDistance)) {
                match = node;
                break;
            }
        }
        if (match) {
            const int32_t count = match->count;
            match->x = (match->x * count + tract[0]) / (count + 1);
            match->z = (match->z * count + tract[1]) / (count + 1);
            match->count = count + 1;
            continue;
        }
        auto* const node = static_cast<Node*>(Allocate(sizeof(Node)));
        node->x = tract[0];
        node->z = tract[1];
        node->count = tract[2];
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;
    }
    for (Node* node = head->next; node != head;) {
        Node* const next = node->next;
        if (node->count < At<int32_t>(self, field::kClusterMinimum)) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            Free(node);
        }
        node = next;
    }
}

// 0x0068D030: a random threshold (0-100) is always drawn; with clusters and
// a riot curve, a random cluster riots (situation 0xCA380B06 at its centre)
// when the curve at its size is above the threshold.
void __fastcall MaybeRiot(void* self, void*)
{
    static bool logged = false;
    FirstCall("MaybeRiot", logged);
    using UniformFn = double(__thiscall*)(void* random, double low, double high);
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
    void* const random = static_cast<char*>(self) + field::kRandom;
    const float threshold = static_cast<float>(reinterpret_cast<UniformFn>(addr::SC4Random_Uniform)(random, 0.0, 100.0));
    void** const head = At<void**>(self, field::kClusters);
    const void* const curve = At<void*>(self, field::kRiotCurve);
    if (static_cast<void**>(head[0]) == head || !curve)
        return;
    uint32_t count = 0;
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
        ++count;
    int32_t pick = static_cast<int32_t>(reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(random, count));
    void** node = static_cast<void**>(head[0]);
    for (; pick > 0; --pick)
        node = static_cast<void**>(node[0]);
    const int32_t* const cluster = reinterpret_cast<const int32_t*>(node + 2);
    if (!(EvaluateCurve(curve, static_cast<float>(cluster[2])) > threshold))
        return;
    const int32_t tractSize = Virtual<int32_t>(At<void*>(self, field::kAuraGrid), 0x28);
    const int32_t x = cluster[0] * tractSize + tractSize / 2;
    const int32_t z = cluster[1] * tractSize + tractSize / 2;
    void* const situations = Virtual<void*>(Service(addr::SC4_City), 0x120);
    if (void* const situation = Virtual<void*>(situations, 0x1C, 0xCA380B06u))
        Virtual<void>(situation, 0x18, x, z);
}

// 0x0068BA40: +0x80 cleared and message 0x2A09B21A sent (with this object
// in both of its pointer fields).
void __fastcall PostPassDone(void* self, void*)
{
    static bool logged = false;
    FirstCall("PostPassDone", logged);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using ConstructFn = void*(__thiscall*)(void* memory);
    At<uint32_t>(self, field::kPassFlag) = 0;
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<ConstructFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, 0x2A09B21Au);
    Virtual<void>(message, 0x30, self);
    Virtual<void>(message, 0x84, self);
    Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0);
    Virtual<uint32_t>(message, 0x8);
}

using ThisFn = void(__fastcall*)(void*, void*);
using OccupantAuraFn = int8_t(__fastcall*)(void*, void*, void*);
OccupantAuraFn g_originalOccupantAura = nullptr;
ThisFn g_originalUpdateEffectValues = nullptr;
ThisFn g_originalUpdateMayorRating = nullptr;
ThisFn g_originalUpdateLowClusters = nullptr;
ThisFn g_originalMaybeRiot = nullptr;
ThisFn g_originalPostPassDone = nullptr;

bool InstallHelpers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    struct Hook {
        std::uintptr_t address;
        void* ours;
        void** original;
    };
    const Hook hooks[] = {
        {addr::cSC4AuraSimulator_OccupantAura, reinterpret_cast<void*>(&OccupantAura),
         reinterpret_cast<void**>(&g_originalOccupantAura)},
        {addr::cSC4AuraSimulator_UpdateEffectValues, reinterpret_cast<void*>(&UpdateEffectValues),
         reinterpret_cast<void**>(&g_originalUpdateEffectValues)},
        {addr::cSC4AuraSimulator_UpdateMayorRating, reinterpret_cast<void*>(&UpdateMayorRating),
         reinterpret_cast<void**>(&g_originalUpdateMayorRating)},
        {addr::cSC4AuraSimulator_UpdateLowClusters, reinterpret_cast<void*>(&UpdateLowClusters),
         reinterpret_cast<void**>(&g_originalUpdateLowClusters)},
        {addr::cSC4AuraSimulator_MaybeRiot, reinterpret_cast<void*>(&MaybeRiot),
         reinterpret_cast<void**>(&g_originalMaybeRiot)},
        {addr::cSC4AuraSimulator_PostPassDone, reinterpret_cast<void*>(&PostPassDone),
         reinterpret_cast<void**>(&g_originalPostPassDone)},
    };
    for (const Hook& h : hooks) {
        const addr::Entry* entry = addr_check::Find(h.address);
        if (!entry || !hook::Install(*entry, h.ours, h.original))
            return false;
    }
    return true;
}

const bool g_registeredHelpers = registry::Register({
    "simulators.aura.helpers",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_OccupantAura,
    "Our aura pass helpers: a lot's aura, the effect values, the mayor rating, low clusters, riots (docs/aura.md)",
    false,
    InstallHelpers,
});

// --- Occupants ------------------------------------------------------------------

// The value and radius of an aura property ({value, radius} as int32s,
// variant slot 105), the value negated for a removal.
const int32_t* AuraProperty(void* property)
{
    return Virtual<const int32_t*>(Virtual<void*>(property, 0x14), 0x1A4);
}

// The cell nearest the occupant's centre along one axis of its lot: the
// lot's middle; with an even size, the lower of the two middle cells
// unless the occupant is nearer the far edge (0x0068ECF0).
int32_t CentreCell(int32_t low, int32_t high, float position, double cellSize)
{
    const int32_t size = high - low + 1;
    if (size % 2 != 1) {
        const double lowEdge = static_cast<double>(low) * cellSize;
        const double nearSide = static_cast<double>(position) - lowEdge;
        const double farSide = static_cast<double>(size) * cellSize + lowEdge - static_cast<double>(position);
        if (farSide > nearSide)
            return size / 2 - 1 + low;
    }
    return size / 2 + low;
}

// 0x0068ECF0: an occupant added or removed. A network piece added
// (0xC772BF98) also makes transient effect 3 where it is. Its park source
// (0x27812850) is spread into +0x54 and its department source
// (0x2781284F) into the department's grid (made on first use, under
// property 0xAA59670C) around its lot's centre; its landmark source
// (0xCA5B9305) into +0x50 around its position. Removal subtracts.
void __fastcall OnOccupant(void* self, void*, void* occupant, uint8_t added)
{
    static bool logged = false;
    FirstCall("OnOccupant", logged);
    float position[3];
    if (added && Virtual<uint32_t>(occupant, 0x1C) == 0xC772BF98u) {
        Virtual<void>(occupant, 0x20, position);
        Virtual<void>(self, 0x18, position[0], position[2], 3u);
    }
    void* const holder = Virtual<void*>(occupant, 0x18);
    void* const park = Virtual<void*>(holder, 0x24, 0x27812850u);
    void* const department = Virtual<void*>(holder, 0x24, 0x2781284Fu);
    if (park || department) {
        Virtual<void>(occupant, 0x20, position);
        void* const city = At<void*>(self, field::kCity);
        int32_t cellX, cellZ;
        Virtual<uint32_t>(city, 0x17C, position[0], position[2], &cellX, &cellZ);
        if (void* const lot = Virtual<void*>(Service(addr::SC4_LotManager), 0x18, cellX, cellZ, 0)) {
            int32_t rect[4];
            Virtual<void>(lot, 0x38, rect);
            cellX = CentreCell(rect[0], rect[2], position[0], Virtual<double>(city, 0x16C));
            cellZ = CentreCell(rect[1], rect[3], position[2], Virtual<double>(city, 0x170));
        }
        if (park) {
            const int32_t* const data = AuraProperty(park);
            const int32_t amount = added ? data[0] : -data[0];
            void* const grid = At<void*>(self, field::kParkSources);
            const float radius = static_cast<float>(data[1]);
            const float tracts =
                static_cast<float>(static_cast<double>(radius) / static_cast<double>(Virtual<int32_t>(grid, 0x28)));
            const int32_t shift = Virtual<int32_t>(grid, 0x2C) & 0x1F;
            const int32_t countZ = Virtual<int32_t>(grid, 0x34);
            const int32_t countX = Virtual<int32_t>(grid, 0x30);
            SpreadAround16(Virtual<void*>(grid, 0x60), countX, countZ, cellX >> shift, cellZ >> shift, tracts,
                           static_cast<float>(amount), 0.0f);
        }
        if (department) {
            using FindFn = void(__thiscall*)(void* map, void** out, const uint32_t* key);
            using AtFn = void**(__thiscall*)(void* map, const uint32_t* key);
            using NewFn = void*(__cdecl*)(std::size_t size);
            using ConstructFn = void*(__thiscall*)(void* memory, uint32_t id);
            const int32_t* const data = AuraProperty(department);
            const int32_t amount = added ? data[0] : -data[0];
            const float radius = static_cast<float>(data[1]);
            uint32_t key = 0, value = 0;
            if (Virtual<uint8_t>(Service(addr::SC4_BudgetSimulator), 0xCC, holder, 0xAA59670Cu, &value))
                key = value;
            void* const map = static_cast<char*>(self) + field::kDepartmentGrids;
            void* node;
            reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(map, &node, &key);
            void* grid = nullptr;
            bool spread = true;
            if (node == At<void*>(self, field::kDepartmentGrids)) {
                if (!added) {
                    spread = false;
                } else {
                    void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x44);
                    grid = memory ? reinterpret_cast<ConstructFn>(addr::cSC4SimGridSint16_Construct)(memory, 0x49B9E600u)
                                  : nullptr;
                    Virtual<uint32_t>(grid, 0x4);
                    Virtual<uint32_t>(grid, 0xC);
                    Virtual<uint32_t>(grid, 0x24, At<uint32_t>(self, field::kGridSize));
                    Virtual<uint32_t>(grid, 0x18, key);
                    *reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(map, &key) = grid;
                }
            } else {
                grid = At<void*>(node, 0x14);
            }
            if (spread) {
                const float tracts = static_cast<float>(static_cast<double>(radius) /
                                                        static_cast<double>(Virtual<int32_t>(grid, 0x28)));
                const int32_t shift = Virtual<int32_t>(grid, 0x2C) & 0x1F;
                const int32_t countZ = Virtual<int32_t>(grid, 0x34);
                const int32_t countX = Virtual<int32_t>(grid, 0x30);
                SpreadAround16(Virtual<void*>(grid, 0x60), countX, countZ, cellX >> shift, cellZ >> shift, tracts,
                               static_cast<float>(amount), 0.0f);
            }
        }
    }
    void* const landmark = Virtual<void*>(holder, 0x24, 0xCA5B9305u);
    if (!landmark)
        return;
    const int32_t* const data = AuraProperty(landmark);
    const int32_t amount = added ? data[0] : -data[0];
    void* const grid = At<void*>(self, field::kLandmarkSources);
    const float radius = static_cast<float>(data[1]);
    const float tracts =
        static_cast<float>(static_cast<double>(radius) / static_cast<double>(Virtual<int32_t>(grid, 0x28)));
    Virtual<void>(occupant, 0x20, position);
    int32_t tractX, tractZ;
    Virtual<uint32_t>(grid, 0x4C, position[0], position[2], &tractX, &tractZ);
    const int32_t countZ = Virtual<int32_t>(grid, 0x34);
    const int32_t countX = Virtual<int32_t>(grid, 0x30);
    SpreadAround8(Virtual<void*>(grid, 0x60), countX, countZ, tractX, tractZ, tracts, static_cast<float>(amount),
                  0.0f);
}

using OnOccupantFn = void(__fastcall*)(void*, void*, void*, uint8_t);
OnOccupantFn g_originalOnOccupant = nullptr;

bool InstallBuildings(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4AuraSimulator_OnOccupant);
    return game::IsBuild641() && entry && hook::Install(*entry, &OnOccupant, &g_originalOnOccupant);
}

const bool g_registeredBuildings = registry::Register({
    "simulators.aura.buildings",
    "simulators/aura",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_OnOccupant,
    "Our aura occupant handler: park, department and landmark aura sources (docs/aura.md)",
    false,
    InstallBuildings,
});

// --- Messages: cIGZMessageTarget2 (vtable 0x00AB07B8) at +0x4 ----------------------

// 0x0068FA60. `part` is the +0x4 interface.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x66956814u: {
        CallThis<void>(addr::cSC4AuraSimulator_Simulate, self, 5u);
        const uint32_t state = At<uint32_t>(self, field::kState);
        if (state != 0 &&
            state < Virtual<uint32_t>(Service(addr::SC4_SimulatorService), 0x20) - At<uint32_t>(self, field::kPassStart))
            return 0;
        return 1;
    }
    case 0x6695681Au:
        CallThis<void>(addr::cSC4AuraSimulator_Simulate, self, Virtual<uint32_t>(message, 0x4C));
        return 1;
    case 0x66956816u: // MonthPassed: a pass starts when none runs
        if (At<uint32_t>(self, field::kState) == 0)
            At<uint32_t>(self, field::kState) = 1;
        return 1;
    case 0x26D31EC1u: {
        if (!At<uint8_t>(self, field::kStrikeFlag))
            return 1;
        using NextFn = uint8_t(__thiscall*)(void* iterator, void** out);
        using ReleaseFn = void(__thiscall*)(void* iterator);
        void* const iterator = Virtual<void*>(Service(addr::SC4_LotManager), 0x3C);
        if (!iterator)
            return 1;
        void* lot;
        while (reinterpret_cast<NextFn>(addr::SC4ListIterator_Next)(iterator, &lot)) {
            void* const building = Virtual<void*>(lot, 0xC0);
            if (!building)
                continue;
            void* const holder = Virtual<void*>(Virtual<void*>(building, 0xC), 0x18);
            if (!Virtual<uint8_t>(holder, 0xC, 0x2781284Fu))
                continue;
            CallThis<void>(addr::cSC4AuraSimulator_OnOccupant, self, Virtual<void*>(building, 0xC), uint8_t{0});
            CallThis<void>(addr::cSC4AuraSimulator_OnOccupant, self, Virtual<void*>(building, 0xC), uint8_t{1});
        }
        reinterpret_cast<ReleaseFn>(addr::SC4ListIterator_Release)(iterator);
        return 1;
    }
    case 0xC2C09554u:
        UpdateRatingOffset(self);
        return 1;
    case 0x99EF1142u:
    case 0x99EF1143u: {
        void* const occupant = Virtual<void*>(message, 0x28);
        void* const holder = Virtual<void*>(occupant, 0x18);
        if (!Virtual<uint8_t>(holder, 0xC, 0x029244DBu))
            CallThis<void>(addr::cSC4AuraSimulator_OnOccupant, self, occupant, uint8_t{type == 0x99EF1142u});
        return 1;
    }
    case 0xC53D10AAu:
        if (Virtual<uint32_t>(message, 0x24) == 0xE7E2C2DBu && Virtual<uint32_t>(message, 0x38) == 0xAA023079u)
            CallThis<void>(0x0068D770, self); // the tuning
        return 1;
    default: return 1;
    }
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4AuraSimulator_vftable_4);
    if (vtable[3] != addr::cSC4AuraSimulator_DoMessage) {
        log::Write("aura messages: slot 3 holds %08X; not installing", static_cast<unsigned>(vtable[3]));
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
    "simulators.aura.messages",
    "simulators/aura",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_vftable_4,
    "Our aura DoMessage (docs/aura.md)",
    false,
    InstallMessages,
});

// --- The interface installation -------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x0068BAB0, GetAuraValue),
    OPENSC4_SLOT(6, 0x0068BAD0, AddTransientEffectOfKind),
    OPENSC4_SLOT(7, 0x0068BB30, AddTransientEffect),
    OPENSC4_SLOT(8, 0x0068BAC0, GetMayorRating),
    OPENSC4_SLOT(9, 0x0068D1E0, GetExtremeRating<field::kBestX>),
    OPENSC4_SLOT(10, 0x0068D200, GetExtremeRating<field::kWorstX>),
    OPENSC4_SLOT(11, 0x0068C0D0, BustStrike),
    OPENSC4_SLOT(12, 0x0068D1B0, FieldGetter<field::kAuraGrid>),
    OPENSC4_SLOT(13, 0x0068D1C0, FieldGetter<field::kTransientGrid>),
    OPENSC4_SLOT(14, 0x0040D390, FieldGetter<field::kParkMap>),
    OPENSC4_SLOT(15, 0x0068D1D0, FieldGetter<field::kLandmarkMap>),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 17;

// Swaps every slot or none: each must still hold the game's function.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4AuraSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("aura interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("aura interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("aura interface: %u of %d cISC4AuraSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.aura.interface",
    "simulators/aura",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4AuraSimulator_vftable,
    "Our cISC4AuraSimulator getters and transient effects on the original object (docs/aura.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::aura_sim
