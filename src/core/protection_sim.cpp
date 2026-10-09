// cSC4ProtectionSimulator, the fire and police simulators' shared base
// (docs/fire-police.md; the name is the one it registers with): abstract,
// at +8 of cSC4FireProtectionSimulator and cSC4PoliceSimulator (constructor 0x006F6900; vtables
// 0x00AB2B10, 0x00AB2ADC, 0x00AB2AC4 before the derived ones replace them).
// It keeps the stations (list at +0x80), their dispatch slots, the coverage
// grids (uint8 at +0x78, int16 at +0x7C) and the walk that averages them,
// and hands the per-cell and per-month work to the derived class through
// its vtable. Offsets here are from the base (derived + 8).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::protection_sim {
namespace {

namespace field {
constexpr std::size_t kCellsPerStep = 0x28; // int32
constexpr std::size_t kSteps = 0x30;        // int32: steps for a whole walk
constexpr std::size_t kWalk = 0x34;         // tract iterator: state, flag, dirty bytes, rect at +0x38
constexpr std::size_t kWalkRect = 0x38;     // int32 x0, z0, x1, z1
constexpr std::size_t kCity = 0x64;
constexpr std::size_t kMessageServer = 0x68;
constexpr std::size_t kBudget = 0x6C;
constexpr std::size_t kService43D14 = 0x70;
constexpr std::size_t kDepartment = 0x74;
constexpr std::size_t kCoverage = 0x78;     // uint8 sim grid
constexpr std::size_t kPower = 0x7C;        // int16 sim grid
constexpr std::size_t kStations = 0x80;     // list of occupants
constexpr std::size_t kCounted = 0x84;      // int32: developed cells this walk
constexpr std::size_t kSum = 0x88;          // int32: their coverage
constexpr std::size_t kCovered = 0x8C;      // int32: those with some
constexpr std::size_t kAverage = 0x90;      // float
constexpr std::size_t kShare = 0x94;        // float
} // namespace field

constexpr uint32_t kRegistered[] = {0x99EF1142, 0x99EF1143, 0xC8DAC052, 0x08DAC3E7, 0xE94671EB,
                                    0x894671F6, 0xE94671EC, 0xE94671ED, 0xC53D10AA};
constexpr uint32_t kUnregistered[] = {0x99EF1142, 0x99EF1143, 0xC8DAC052, 0x08DAC3E7, 0xE94671EB,
                                      0xE94671EC, 0xE94671ED, 0x894671F6, 0xC53D10AA};

using SelfFn = void(__thiscall*)(void* self);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Hold(void* self, std::size_t at, void* object)
{
    At<void*>(self, at) = object;
    Virtual<uint32_t>(object, 0x4); // AddRef
}

// --- DoMessage (0x006F62C0, base slot 3) ----------------------------------------

// Whether an occupant message is about one marked with property 0x029244DB
// (those are left alone).
bool Marked(void* occupant)
{
    void* const holder = Virtual<void*>(occupant, 0x18);
    return Virtual<uint8_t>(holder, 0xC, 0x029244DBu) != 0;
}

// Whether a dispatch message is for this simulator: its first value (read
// first, as the game does) against the derived class's id (slot 13).
bool Ours(char* self, void* message)
{
    const uint32_t id = Virtual<uint32_t>(message, 0x24);
    return id == Virtual<uint32_t>(self, 0x34);
}

uint8_t __fastcall DoMessage(char* self, void*, void* message)
{
    using DispatchFn = void(__thiscall*)(void* self, uint32_t id);
    using StrikeFn = void(__thiscall*)(void* self, uint32_t id, uint32_t start);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x66956810:
        return 1;
    case 0x66956814: // the step
        Virtual<void>(self, 0x14, Virtual<uint32_t>(message, 0x24));
        return 1;
    case 0x66956816: // the month
        Virtual<void>(self, 0x18);
        return 1;
    case 0x08DAC3E7: { // funding changed
        const uint32_t department = Virtual<uint32_t>(At<void*>(self, field::kDepartment), 0xC);
        if (Virtual<uint32_t>(message, 0x38) == department) {
            char* const stations = At<char*>(self, field::kStations);
            if (At<char*>(stations, 0) != stations)
                reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_PaintCoverage)(self);
        }
        return 1;
    }
    case 0x99EF1142:
    case 0x99EF1143:
    case 0xC8DAC052: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!Marked(occupant))
            Virtual<void>(self, type == 0x99EF1142 ? 0x1C : type == 0x99EF1143 ? 0x20 : 0x24, occupant);
        return 1;
    }
    case 0x894671F6:
        if (Ours(self, message))
            reinterpret_cast<DispatchFn>(addr::cSC4ProtectionSimulator_OnDispatch)(self, Virtual<uint32_t>(message, 0x38));
        return 1;
    case 0xE94671EB:
        if (Ours(self, message))
            Virtual<uint32_t>(message, 0x38);
        return 1;
    case 0xE94671EC:
    case 0xE94671ED:
        if (Ours(self, message))
            reinterpret_cast<StrikeFn>(addr::cSC4ProtectionSimulator_OnDispatchMove)(self, Virtual<uint32_t>(message, 0x38),
                                                                                  type == 0xE94671EC ? 1u : 0u);
        return 1;
    default:
        return 0;
    }
}

// --- The walk (0x006F6A70, base slot 5) ---------------------------------------

// Per step, +0x28 cells of the coverage walk: each cell's coverage byte from
// the power grid (0x006F5E40), the derived class told (slot 16), and the
// developed cells (a block of the +0x70 service's grid with something in
// it) counted, summed and counted when covered. At the walk's end the
// average and the share covered, the derived class told (slot 17), and the
// walk restarted.
void __fastcall Step(char* self, void*, uint32_t)
{
    using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    using RestartFn = void(__thiscall*)(void* iterator, uint32_t mode);
    using CellFn = uint32_t(__thiscall*)(void* self, int32_t x, int32_t z, void* power, void* coverage);
    using DevelopedFn = uint8_t(__thiscall*)(void* self, int32_t x, int32_t z, void* grid);
    uint32_t left = At<uint32_t>(self, field::kCellsPerStep);
    void* const coverage = Virtual<void*>(At<void*>(self, field::kCoverage), 0x60);
    void* const power = Virtual<void*>(At<void*>(self, field::kPower), 0x60);
    void* const developed = Virtual<void*>(Virtual<void*>(At<void*>(self, field::kService43D14), 0x14), 0x64);
    while (left) {
        int32_t x = 0, z = 0;
        if (reinterpret_cast<NextFn>(addr::TractIterator_Next)(self + field::kWalk, &x, &z)) {
            const uint32_t value =
                reinterpret_cast<CellFn>(addr::cSC4ProtectionSimulator_CoverageCell)(self, x, z, power, coverage);
            Virtual<void>(self, 0x40, x, z, value);
            if (reinterpret_cast<DevelopedFn>(addr::cSC4ProtectionSimulator_BlockDeveloped)(self, x, z, developed)) {
                ++At<int32_t>(self, field::kCounted);
                At<int32_t>(self, field::kSum) += static_cast<int32_t>(value);
                if (value)
                    ++At<int32_t>(self, field::kCovered);
            }
            --left;
            continue;
        }
        const int32_t counted = At<int32_t>(self, field::kCounted);
        if (counted > 0) {
            At<float>(self, field::kAverage) =
                static_cast<float>(static_cast<double>(At<int32_t>(self, field::kSum)) / counted);
            At<float>(self, field::kShare) =
                static_cast<float>(static_cast<double>(At<int32_t>(self, field::kCovered)) / counted);
        } else {
            At<uint32_t>(self, field::kAverage) = 0;
            At<uint32_t>(self, field::kShare) = Global<uint32_t>(addr::cSC4ProtectionSimulator_DefaultShare);
        }
        At<int32_t>(self, field::kCounted) = 0;
        At<int32_t>(self, field::kSum) = 0;
        At<int32_t>(self, field::kCovered) = 0;
        Virtual<void>(self, 0x44);
        reinterpret_cast<RestartFn>(addr::TractIterator_Restart)(self + field::kWalk, 1);
    }
}

// --- Coverage ----------------------------------------------------------------------

// A float rounded to an integer the game's way: added to 1.5 x 2^23,
// stored as a float, and the bits read back.
int32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof(bits));
    return static_cast<int32_t>(bits + 0xB4C00000);
}

template <typename T>
T& Cell(void* rows, int32_t x, int32_t z)
{
    return reinterpret_cast<T*>(static_cast<char***>(rows)[0][3 * x])[z];
}

// The coverage byte of a cell: its power x +0x98, rounded, 0 to 255.
uint8_t CoverageOf(char* self, void* power, int32_t x, int32_t z)
{
    const int32_t value =
        MagicRound(static_cast<double>(Cell<int16_t>(power, x, z)) * At<float>(self, 0x98));
    return static_cast<uint8_t>(value > 0xFF ? 0xFF : value < 0 ? 0 : value);
}

// 0x006F5E40: a cell's coverage byte written and returned.
uint32_t __fastcall CoverageCell(char* self, void*, int32_t x, int32_t z, void* power, void* coverage)
{
    const uint8_t value = CoverageOf(self, power, x, z);
    Cell<uint8_t>(coverage, x, z) = value;
    return value;
}

// 0x006F6180: whether any byte of the +0x2C-cell block (x, z) is set.
uint8_t __fastcall BlockDeveloped(char* self, void*, int32_t x, int32_t z, void* grid)
{
    const int32_t size = At<int32_t>(self, 0x2C);
    for (int32_t row = x * size; row < (x + 1) * size; ++row)
        for (int32_t column = z * size; column < (z + 1) * size; ++column)
            if (Cell<uint8_t>(grid, row, column))
                return 1;
    return 0;
}

// 0x006F5EB0: the coverage bytes of the square around (x, z), within the
// grid.
void __fastcall CopyCoverage(char* self, void*, int32_t x, int32_t z, int32_t radius, void* power, void* coverage)
{
    int32_t x0 = x - radius, z0 = z - radius, x1 = x + radius, z1 = z + radius;
    if (x0 < 0)
        x0 = 0;
    if (z0 < 0)
        z0 = 0;
    void* const grid = At<void*>(self, field::kCoverage);
    if (x1 > Virtual<int32_t>(grid, 0x30) - 1)
        x1 = Virtual<int32_t>(grid, 0x30) - 1;
    if (z1 > Virtual<int32_t>(grid, 0x34) - 1)
        z1 = Virtual<int32_t>(grid, 0x34) - 1;
    for (int32_t row = x0; row <= x1; ++row)
        for (int32_t column = z0; column <= z1; ++column)
            Cell<uint8_t>(coverage, row, column) = CoverageOf(self, power, row, column);
}

// 0x006F6110: the power grid painted again, then the coverage bytes around
// (x, z) (a cell, made a tract), radius x the grid's scale (truncated).
void __fastcall UpdateAround(char* self, void*, int32_t x, int32_t z, float radius)
{
    void* const grid = At<void*>(self, field::kCoverage);
    Virtual<void>(grid, 0x4C, x, z, &x, &z);
    const double scaled = Virtual<double>(At<void*>(self, field::kCoverage), 0x40) * radius;
    const auto tracts = static_cast<int32_t>(scaled);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_PaintCoverage)(self);
    void* const coverage = Virtual<void*>(At<void*>(self, field::kCoverage), 0x60);
    void* const power = Virtual<void*>(At<void*>(self, field::kPower), 0x60);
    CopyCoverage(self, nullptr, x, z, tracts, power, coverage);
}

// 0x006F5C30: the power grid (+0x7C) cleared and painted again: around
// each station (slot 14's position, reach, value and edge) and each active
// source the dispatch service lists for this simulator (slot 12's reach),
// positions and reach scaled by the grid's scale (slot 16).
void __fastcall PaintCoverage(char* self, void*)
{
    using SpreadFn = void(__cdecl*)(void* grid, int32_t tractsX, int32_t tractsZ, int32_t tractX, int32_t tractZ,
                                    float radius, float value, float edge);
    const auto spread = reinterpret_cast<SpreadFn>(addr::SC4SimGrid_SpreadAround);
    void* const power = At<void*>(self, field::kPower);
    const auto scale = static_cast<float>(Virtual<double>(power, 0x40));
    const int32_t width = Virtual<int32_t>(At<void*>(self, field::kPower), 0x30);
    const int32_t depth = Virtual<int32_t>(At<void*>(self, field::kPower), 0x34);
    Virtual<void>(At<void*>(self, field::kPower), 0x6C, 0u);
    void* const rows = Virtual<void*>(At<void*>(self, field::kPower), 0x60);
    auto paint = [&](float x, float z, uint8_t* reach) {
        const auto tractZ = static_cast<int32_t>(static_cast<double>(z) * scale);
        const auto tractX = static_cast<int32_t>(static_cast<double>(x) * scale);
        spread(rows, width, depth, tractX, tractZ, static_cast<float>(static_cast<double>(At<float>(reach, 8)) * scale),
               static_cast<float>(At<int32_t>(reach, 0xC)), static_cast<float>(At<int32_t>(reach, 0x10)));
    };
    char* const head = At<char*>(self, field::kStations);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        uint8_t info[0x18] = {};
        Virtual<uint8_t>(self, 0x38, At<void*>(node, 8), info);
        paint(At<float>(info, 0), At<float>(info, 4), info);
    }

    void* const service = Global<void*>(addr::SC4_DispatchService);
    if (!service)
        return;
    const uint32_t id = Virtual<uint32_t>(self, 0x34);
    uint32_t count = Virtual<uint32_t>(service, 0x2C, id);
    uint32_t inline_[0x40];
    uint32_t* sources = inline_;
    uint32_t* allocated = nullptr;
    if (count >= 0x40) {
        using NewFn = void*(__cdecl*)(std::size_t size);
        allocated = static_cast<uint32_t*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(count * 4));
        sources = allocated;
    }
    if (count > 0) {
        Virtual<uint8_t>(service, 0x30, id, sources, &count);
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t source[0x28] = {};
            if (!Virtual<uint8_t>(service, 0x28, sources[i], source) || !source[0x10])
                continue;
            uint8_t reach[0x18] = {};
            Virtual<uint8_t>(self, 0x30, At<uint32_t>(source, 0x20), reach);
            paint(At<float>(source, 0x14), At<float>(source, 0x1C), reach);
        }
    }
    if (allocated)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(allocated);
}

// --- Dispatch messages -------------------------------------------------------------

// 0x006F5A40 (message 0x894671F6): the dispatch source's entry; its +0x20
// and +8 to 0x006F5620.
void __fastcall OnDispatch(char* self, void*, uint32_t id)
{
    using MarkFn = uint8_t(__thiscall*)(void* self, uint32_t a, uint32_t b);
    void* const service = Global<void*>(addr::SC4_DispatchService);
    if (!service)
        return;
    uint8_t info[0x2C] = {};
    if (!Virtual<uint8_t>(service, 0x28, id, info))
        return;
    reinterpret_cast<MarkFn>(addr::cSC4ProtectionSimulator_ReturnUnit)(self, At<uint32_t>(info, 0x20),
                                                                         At<uint32_t>(info, 8));
}

// 0x006F61F0 (messages 0xE94671EC, 0xE94671ED): the dispatch source's
// entry; the derived class's reach for its +0x20 (slot 12), and the
// coverage updated around the entry's cell.
void __fastcall OnDispatchMove(char* self, void*, uint32_t id, uint32_t)
{
    void* const service = Global<void*>(addr::SC4_DispatchService);
    if (!service)
        return;
    uint8_t info[0x28] = {};
    if (Virtual<uint8_t>(service, 0x28, id, info) != 1)
        return;
    uint8_t reach[0x18] = {};
    Virtual<uint8_t>(self, 0x30, At<uint32_t>(info, 0x20), reach);
    UpdateAround(self, nullptr, At<int32_t>(info, 0x14), At<int32_t>(info, 0x1C), At<float>(reach, 8));
}

// --- Units ------------------------------------------------------------------------

// A station's units, as +4's slot 7 gives them: a mask with a bit per unit
// at home, the number of its first dispatch slot, and how many it has.
// Each out-parameter is left alone when the derived class has nothing to
// say, so callers set them first as the game does.

// 0x006F5620: a unit back at its station: its slot's bit set (slot 10),
// unless every unit is home already or this one is.
uint8_t __fastcall ReturnUnit(char* self, void*, void* station, uint32_t slot)
{
    // The game keeps the mask where the station argument was, and the
    // count in a slot it never sets.
    uint32_t mask = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(station));
    uint32_t first = 0;
    uint32_t count = 0;
    Virtual<uint8_t>(self + 4, 0x1C, station, &mask, &first, &count);
    uint32_t home = 0;
    for (uint32_t bit = 1; bit; bit <<= 1)
        if (mask & bit)
            ++home;
    if (home >= count)
        return 0;
    const uint32_t bit = 1u << ((slot - first) & 31);
    if (mask & bit)
        return 0;
    Virtual<uint32_t>(self, 0x28, station, mask | bit);
    return 1;
}

// 0x006F5570 (+4 slot 9): a unit leaves its station: the given slot's bit
// cleared (out of range: nothing), or for slot 0 the first unit at home
// (none: the last bit), and the slot written back.
uint8_t __fastcall SendUnit(char* units, void*, void* station, uint32_t* slot)
{
    // As in ReturnUnit: the count where the station argument was.
    uint32_t first = 0;
    uint32_t mask = 0;
    uint32_t count = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(station));
    Virtual<uint8_t>(units, 0x1C, station, &mask, &first, &count);
    uint32_t offset = 0;
    uint32_t bit;
    if (*slot != 0) {
        offset = *slot - first;
        if (offset > count)
            return 0;
        bit = 1u << (offset & 31);
    } else if (count == 0) {
        bit = 0;
    } else {
        do {
            bit = 1u << (offset & 31);
            if (mask & bit)
                break;
        } while (++offset < count);
    }
    Virtual<uint32_t>(units - 4, 0x28, station, mask & ~bit);
    *slot = first + offset;
    return 1;
}

// 0x006F5B00 (+4 slot 5): the stations' units summed. The count is set to 0
// once, as the game does.
int32_t __fastcall UnitCount(char* units, void*)
{
    char* const head = At<char*>(units, field::kStations - 4);
    int32_t total = 0;
    uint32_t count = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        Virtual<uint8_t>(units, 0x1C, At<void*>(node, 8), nullptr, nullptr, &count);
        total += static_cast<int32_t>(count);
    }
    return total;
}

// 0x006F5A80 (+4 slot 4): whether a call at (x, z) can be taken (some
// units, and land above the water level); busy when the dispatch service
// has as many out for this simulator (slot 13's id) as there are units.
uint8_t __fastcall CanDispatch(char* units, void*, float x, float z, uint8_t* busy)
{
    const int32_t total = UnitCount(units, nullptr);
    if (total <= 0)
        return 0;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!terrain)
        return 0;
    const float height = static_cast<float>(Virtual<double>(terrain, 0x34, x, z));
    if (!(Virtual<double>(terrain, 0x74) < height))
        return 0;
    void* const service = Global<void*>(addr::SC4_DispatchService);
    if (!service)
        return 0;
    const uint32_t id = Virtual<uint32_t>(units - 4, 0x34);
    *busy = Virtual<uint32_t>(service, 0x2C, id) >= static_cast<uint32_t>(total);
    return 1;
}

// 0x006F5B40 (+4 slot 8): the nearest station to (x, z) with a unit home
// (and, if the lot manager has it, its slot 28 allowing it); a unit sent
// from it (+4 slot 9) into *slot.
void* __fastcall NearestStation(char* units, void*, float x, float z, uint32_t* slot)
{
    const float point[2] = {x, z};
    char* const head = At<char*>(units, field::kStations - 4);
    float best = 3.40282347e+38f;
    void* nearest = nullptr;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const station = At<void*>(node, 8);
        uint32_t mask = 0;
        Virtual<uint8_t>(units, 0x1C, station, &mask, nullptr, nullptr);
        if (!mask)
            continue;
        if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, station))
            if (!Virtual<uint8_t>(lot, 0x70))
                continue;
        float position[3];
        Virtual<uint32_t>(station, 0x20, position);
        const double dx = static_cast<double>(point[0]) - position[0];
        const double dz = static_cast<double>(point[1]) - position[2];
        const double distance = std::sqrt(dz * dz + dx * dx);
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = station;
        }
    }
    *slot = 0;
    if (nearest)
        Virtual<uint8_t>(units, 0x24, nearest, slot);
    return nearest;
}

// 0x006DFC70 (`this` unused): out emptied, then each occupant of in held
// and appended; how many.
int32_t __fastcall CopyOccupantList(void*, void*, void** out, void** in)
{
    reinterpret_cast<SelfFn>(addr::SC4List_Clear2)(out);
    int32_t count = 0;
    char* const head = static_cast<char*>(*in);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        Virtual<uint32_t>(occupant, 0x4); // AddRef
        char* const end = static_cast<char*>(*out);
        auto* const added = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
        At<void*>(added, 8) = occupant;
        char* const last = At<char*>(end, 4);
        At<char*>(added, 4) = last;
        At<char*>(added, 0) = end;
        At<char*>(last, 0) = added;
        At<char*>(end, 4) = added;
        ++count;
    }
    return count;
}

// 0x006F6A50 (+4 slot 6): the stations copied into the caller's list;
// whether there are any.
uint32_t __fastcall GetStations(char* units, void*, void** list)
{
    return CopyOccupantList(units - 4, nullptr, list, reinterpret_cast<void**>(units + field::kStations - 4)) > 0;
}

// 0x006F56A0 (`this` unused): a dispatch info's reach (+8) times a factor,
// its +0xC and +0x10 too (truncated).
void __fastcall ScaleDispatchInfo(void*, void*, char* info, float factor)
{
    At<float>(info, 8) = factor * At<float>(info, 8);
    At<int32_t>(info, 0xC) = static_cast<int32_t>(static_cast<double>(At<int32_t>(info, 0xC)) * factor);
    At<int32_t>(info, 0x10) = static_cast<int32_t>(static_cast<double>(At<int32_t>(info, 0x10)) * factor);
}

// --- Stations ---------------------------------------------------------------------

constexpr uint32_t kBuildingType = 0x278128A0;

// A station's position and reach, as the derived class's slot 14 gives it.
struct StationInfo {
    int32_t x;
    int32_t z;
    float radius;
    uint8_t rest[0x18];
};

char* FindStation(char* self, void* occupant)
{
    char* const head = At<char*>(self, field::kStations);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        if (At<void*>(node, 8) == occupant)
            return node;
    return nullptr;
}

// The station's line item in the budget (+0x6C slots 51 and 28, found by
// the station's properties and the kind at +0xA8), told of the station
// through its slot 22 (added) or 23 (removed).
void TellLineItem(char* self, void* occupant, std::size_t slot)
{
    uint8_t item[0x20]; // the game sets only +0x1C
    At<uint32_t>(item, 0x1C) = 0;
    const uint32_t kind = At<uint32_t>(self, 0xA8);
    void* const budget = At<void*>(self, field::kBudget);
    if (!Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(occupant, 0x18), kind, item))
        return;
    void* const line = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, At<uint32_t>(item, 0));
    if (line)
        Virtual<uint32_t>(line, slot, Virtual<void*>(occupant, 0x18), At<uint32_t>(self, 0xA8));
}

// The game's std::vector<bool> (start word, start bit, end word, end bit,
// capacity).
struct BitVector {
    uint32_t* start;
    int32_t startBit;
    uint32_t* end;
    int32_t endBit;
    uint32_t* capacity;
    uint32_t* WordOf(int32_t index, uint32_t* bit) const
    {
        const int32_t at = startBit + index;
        *bit = 1u << (at % 32);
        return start + at / 32;
    }
};

// 0x006F6610 (slot 7): a building that is a station (slot 14 says so)
// joins the list, takes the first run of free dispatch slots after slot 0
// (as many as +4's slot 7 counts for it; slots 10 and 15 told), tells its
// line item, and the coverage around it is painted again.
void __fastcall OnStationAdded(char* self, void*, void* occupant)
{
    using NodesFn = char*(__thiscall*)(void* list, uint32_t count, uint32_t unused);
    using ResizeFn = void(__thiscall*)(BitVector* bits, uint32_t count, uint32_t value);
    if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
        return;
    StationInfo info{};
    if (!Virtual<uint8_t>(self, 0x38, occupant, &info))
        return;
    Virtual<uint32_t>(occupant, 0x4); // AddRef
    char* const head = At<char*>(self, field::kStations);
    char* const node = reinterpret_cast<NodesFn>(addr::SC4List_AllocateNodes)(self + field::kStations, 1, 0);
    if (node + 8)
        At<void*>(node, 8) = occupant;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 4) = last;
    At<char*>(node, 0) = head;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;

    char* const dispatch = self + 4;
    uint32_t count = 0;
    Virtual<uint8_t>(dispatch, 0x1C, occupant, 0u, 0u, &count);
    Virtual<uint8_t>(self, 0x28, occupant, (1u << count) - 1);
    BitVector taken{};
    reinterpret_cast<ResizeFn>(addr::SC4BitVector_Resize)(&taken, At<uint32_t>(self, 0xA4), 0);
    for (char* at = At<char*>(head, 0); at != head; at = At<char*>(at, 0)) {
        void* const other = At<void*>(at, 8);
        if (other == occupant)
            continue;
        uint32_t first = 0, slots = 0;
        Virtual<uint8_t>(dispatch, 0x1C, other, 0u, &first, &slots);
        for (uint32_t i = 0; i < slots; ++i) {
            uint32_t bit;
            *taken.WordOf(static_cast<int32_t>(first + i), &bit) |= bit;
        }
    }
    uint32_t start = 1;
    while (start < At<uint32_t>(self, 0xA4)) {
        uint32_t clash = 0;
        bool found = false;
        for (; clash < count; ++clash) {
            uint32_t bit;
            if (*taken.WordOf(static_cast<int32_t>(clash + start), &bit) & bit) {
                found = true;
                break;
            }
        }
        if (!found || clash >= count)
            break;
        start += clash + 1;
    }
    Virtual<uint8_t>(self, 0x3C, occupant, start);
    if (At<uint32_t>(self, 0xA4) < start + count)
        At<uint32_t>(self, 0xA4) = start + count;
    TellLineItem(self, occupant, 0x58);
    const float radius = info.radius;
    int32_t x = 0, z = 0;
    Virtual<void>(At<void*>(self, field::kCoverage), 0x4C, info.x, info.z, &x, &z);
    const double scaled = Virtual<double>(At<void*>(self, field::kCoverage), 0x40) * radius;
    const auto tracts = static_cast<int32_t>(scaled);
    reinterpret_cast<SelfFn>(addr::cSC4ProtectionSimulator_PaintCoverage)(self);
    void* const coverage = Virtual<void*>(At<void*>(self, field::kCoverage), 0x60);
    void* const power = Virtual<void*>(At<void*>(self, field::kPower), 0x60);
    CopyCoverage(self, nullptr, x, z, tracts, power, coverage);
    if (taken.start)
        Free(taken.start);
}

// 0x006F64F0 (slot 8): a station leaves the list, its line item is told,
// the coverage around it is painted again, and it is released.
void __fastcall OnStationRemoved(char* self, void*, void* occupant)
{
    using EraseFn = void(__thiscall*)(void* list, char** next, char* node);
    if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
        return;
    char* const node = FindStation(self, occupant);
    if (!node)
        return;
    char* next = nullptr;
    reinterpret_cast<EraseFn>(addr::SC4List_Erase)(self + field::kStations, &next, node);
    TellLineItem(self, occupant, 0x5C);
    StationInfo info{};
    Virtual<uint8_t>(self, 0x38, occupant, &info);
    UpdateAround(self, nullptr, info.x, info.z, info.radius);
    Virtual<uint32_t>(occupant, 0x8); // Release
}

// 0x006F6250 (slot 9): a listed station changed: the coverage around it is
// painted again.
void __fastcall OnStationChanged(char* self, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
        return;
    if (!FindStation(self, occupant))
        return;
    StationInfo info{};
    Virtual<uint8_t>(self, 0x38, occupant, &info);
    UpdateAround(self, nullptr, info.x, info.z, info.radius);
}

// --- Init (0x006F6BC0) and Shutdown (0x006F56F0) --------------------------------

// A sim grid as the game builds them inline (operator new of 0x44 bytes,
// only these fields set).
void* NewGrid(std::uintptr_t vtable, std::uintptr_t vtable2, uint32_t id)
{
    auto* const grid = static_cast<uint32_t*>(OperatorNew(0x44));
    if (!grid)
        return nullptr;
    grid[1] = static_cast<uint32_t>(addr::SC4SimGrid_VtableBase);
    grid[0] = static_cast<uint32_t>(vtable);
    grid[1] = static_cast<uint32_t>(vtable2);
    reinterpret_cast<uint8_t*>(grid)[8] = 0;
    grid[3] = 0;
    grid[4] = id;
    grid[6] = 0;
    grid[14] = 0;
    grid[15] = 0;
    grid[16] = 0;
    return grid;
}

void SetUpGrid(char* self, std::size_t at, std::uintptr_t vtable, std::uintptr_t vtable2, uint32_t id)
{
    if (At<void*>(self, at))
        return;
    void* const grid = NewGrid(vtable, vtable2, id);
    At<void*>(self, at) = grid;
    Virtual<uint32_t>(grid, 0x4); // AddRef
    Virtual<uint8_t>(At<void*>(self, at), 0xC);
    Virtual<void>(At<void*>(self, at), 0x24, At<uint32_t>(self, 0x2C));
    Virtual<void>(At<void*>(self, at), 0x6C, 0u);
}

// The game's cIGZString over a buffer.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("protection sim: first Init (%p)", static_cast<void*>(self));
    }
    if (!At<void*>(self, field::kCity))
        Hold(self, field::kCity, Global<void*>(addr::SC4_City));
    if (!At<void*>(self, field::kBudget))
        Hold(self, field::kBudget, Virtual<void*>(At<void*>(self, field::kCity), 0xD0));
    if (!At<void*>(self, field::kService43D14))
        Hold(self, field::kService43D14, Global<void*>(addr::SC4_Service43D14));
    SetUpGrid(self, field::kCoverage, addr::SC4SimGridUint8_Vtable, addr::SC4SimGridUint8_Vtable2, 0x49B9E602);
    SetUpGrid(self, field::kPower, addr::SC4SimGridSint16_Vtable, addr::SC4SimGridSint16_Vtable2, 0x49B9E605);
    const int32_t lastZ = Virtual<int32_t>(At<void*>(self, field::kCoverage), 0x34) - 1;
    const int32_t lastX = Virtual<int32_t>(At<void*>(self, field::kCoverage), 0x30) - 1;
    auto* const rect = &At<int32_t>(self, field::kWalkRect);
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = lastX;
    rect[3] = lastZ;
    if (rect[2] < rect[0])
        rect[2] = rect[0];
    if (rect[3] < rect[1])
        rect[3] = rect[1];
    self[field::kWalk + 2] = 1;
    if (void* const simulators = Global<void*>(addr::SC4_SimulatorService)) {
        using FromTextFn = void(__thiscall*)(GameString* text, const char* value);
        GameString name{};
        reinterpret_cast<FromTextFn>(addr::SC4String_FromText)(&name,
                                                              reinterpret_cast<const char*>(addr::cSC4ProtectionSimulator_Name));
        Virtual<uint32_t>(simulators, 0x4C, self, 4u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, self, 6u, &name, 2u);
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            Free(name.begin);
    }
    if (!At<void*>(self, field::kMessageServer)) {
        Hold(self, field::kMessageServer, Global<void*>(addr::SC4_MessageServer));
        for (uint32_t message : kRegistered)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, self, message);
    }
    Virtual<uint32_t>(Global<void*>(addr::SC4_DispatchService), 0x34, self + 4);
    void* const grid = At<void*>(self, field::kCoverage);
    const int32_t depth = Virtual<int32_t>(grid, 0x34);
    const int32_t width = Virtual<int32_t>(grid, 0x30);
    At<int32_t>(self, field::kCellsPerStep) = width * depth / At<int32_t>(self, field::kSteps);
    return 1;
}

uint8_t __fastcall Shutdown(char* self, void*)
{
    void* const city = Global<void*>(addr::SC4_City);
    Virtual<uint32_t>(Virtual<void*>(city, 0x9C), 0x38, self ? self + 4 : nullptr);
    Virtual<uint32_t>(Virtual<void*>(city, 0x88), 0xC0, self + field::kStations);
    if (void* const server = At<void*>(self, field::kMessageServer)) {
        for (uint32_t message : kUnregistered)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, self, message);
        Release(At<void*>(self, field::kMessageServer));
        At<void*>(self, field::kMessageServer) = nullptr;
        (void)server;
    }
    if (void* const zone = Virtual<void*>(city, 0xC8))
        Virtual<uint32_t>(zone, 0x50, self);
    for (const std::size_t at : {field::kCoverage, field::kPower}) {
        if (At<void*>(self, at)) {
            Virtual<uint8_t>(At<void*>(self, at), 0x10);
            Release(At<void*>(self, at));
            At<void*>(self, at) = nullptr;
        }
    }
    for (const std::size_t at : {field::kService43D14, field::kBudget, field::kCity}) {
        if (void* const held = At<void*>(self, at)) {
            Release(held);
            At<void*>(self, at) = nullptr;
        }
    }
    return 1;
}

// --- Write (0x006F5850) and Read (0x006F5920) -----------------------------------

// cIGZSerializable slots 3 and 4; `this` is the serializable part at +8.
// Called by the fire and police Write/Read for their first part.
constexpr std::size_t kSerializable = 8;
constexpr uint32_t kDepartmentInterface = 0x223E2B67;
constexpr uint32_t kGridInterface = 0xC9BD939A;

using StreamObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
using StreamBoolFn = void*(__cdecl*)(void* stream, uint32_t value);
using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);

// Whether the stream had no error (slot 18), as 1 or 0.
uint32_t StreamOk(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

uint32_t __fastcall Write(char* serializable, void*, void* stream)
{
    char* const self = serializable - kSerializable;
    const auto writeReference = reinterpret_cast<StreamObjectFn>(addr::SC4Stream_WriteReference);
    Virtual<uint8_t>(stream, 0x1C, uint16_t{3}); // version
    writeReference(stream, At<void*>(self, field::kDepartment));
    reinterpret_cast<StreamObjectFn>(addr::SC4Stream_WritePollutionStruct)(stream, self + field::kWalk);
    writeReference(stream, At<void*>(self, field::kCoverage));
    writeReference(stream, At<void*>(self, field::kPower));
    reinterpret_cast<StreamObjectFn>(addr::SC4Stream_WriteReferenceList)(stream, self + field::kStations);
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kCounted));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kSum));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, field::kCovered));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kAverage)); // the floats' bits
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kShare));
    reinterpret_cast<StreamBoolFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, 0x9C));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, 0xA0));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, 0xA4));
    return StreamOk(stream);
}

uint32_t __fastcall Read(char* serializable, void*, void* stream)
{
    char* const self = serializable - kSerializable;
    const auto readReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > 3)
        return 0;
    readReference(stream, kDepartmentInterface, &At<void*>(self, field::kDepartment), 0);
    reinterpret_cast<StreamObjectFn>(addr::SC4Stream_ReadPollutionStruct)(stream, self + field::kWalk);
    readReference(stream, kGridInterface, &At<void*>(self, field::kCoverage), 0);
    readReference(stream, kGridInterface, &At<void*>(self, field::kPower), 0);
    reinterpret_cast<StreamObjectFn>(addr::SC4Stream_ReadReferenceList)(stream, self + field::kStations);
    Virtual<uint8_t>(stream, 0x20, self + field::kCounted);
    Virtual<uint8_t>(stream, 0x20, self + field::kSum);
    Virtual<uint8_t>(stream, 0x20, self + field::kCovered);
    Virtual<uint8_t>(stream, 0x30, self + field::kAverage);
    // Before version 3 the share was a percentage.
    float share = 0;
    Virtual<uint8_t>(stream, 0x30, &share);
    if (version >= 3)
        std::memcpy(self + field::kShare, &share, 4);
    else
        At<float>(self, field::kShare) = static_cast<float>(static_cast<double>(share) * static_cast<double>(0.01f));
    reinterpret_cast<StreamObjectFn>(addr::SC4Stream_ReadBool)(stream, self + 0x9C);
    Virtual<uint8_t>(stream, 0x24, self + 0xA0);
    Virtual<uint8_t>(stream, 0x24, self + 0xA4);
    return StreamOk(stream);
}

// Hooks to leave out, for bisecting: OPENSC4_PROTECTION_SKIP lists function
// addresses (hex).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_PROTECTION_SKIP", list, sizeof list))
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
        {addr::cSC4ProtectionSimulator_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4ProtectionSimulator_Step, reinterpret_cast<void*>(&Step)},
        {addr::cSC4ProtectionSimulator_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4ProtectionSimulator_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4ProtectionSimulator_CoverageCell, reinterpret_cast<void*>(&CoverageCell)},
        {addr::cSC4ProtectionSimulator_BlockDeveloped, reinterpret_cast<void*>(&BlockDeveloped)},
        {addr::cSC4ProtectionSimulator_CopyCoverage, reinterpret_cast<void*>(&CopyCoverage)},
        {addr::cSC4ProtectionSimulator_UpdateAround, reinterpret_cast<void*>(&UpdateAround)},
        {addr::cSC4ProtectionSimulator_OnDispatch, reinterpret_cast<void*>(&OnDispatch)},
        {addr::cSC4ProtectionSimulator_OnDispatchMove, reinterpret_cast<void*>(&OnDispatchMove)},
        {addr::cSC4ProtectionSimulator_OnStationAdded, reinterpret_cast<void*>(&OnStationAdded)},
        {addr::cSC4ProtectionSimulator_OnStationRemoved, reinterpret_cast<void*>(&OnStationRemoved)},
        {addr::cSC4ProtectionSimulator_OnStationChanged, reinterpret_cast<void*>(&OnStationChanged)},
        {addr::cSC4ProtectionSimulator_PaintCoverage, reinterpret_cast<void*>(&PaintCoverage)},
        {addr::cSC4ProtectionSimulator_ReturnUnit, reinterpret_cast<void*>(&ReturnUnit)},
        {addr::cSC4ProtectionSimulator_SendUnit, reinterpret_cast<void*>(&SendUnit)},
        {addr::cSC4ProtectionSimulator_UnitCount, reinterpret_cast<void*>(&UnitCount)},
        {addr::cSC4ProtectionSimulator_CanDispatch, reinterpret_cast<void*>(&CanDispatch)},
        {addr::cSC4ProtectionSimulator_NearestStation, reinterpret_cast<void*>(&NearestStation)},
        {addr::cSC4ProtectionSimulator_GetStations, reinterpret_cast<void*>(&GetStations)},
        {addr::cSC4ProtectionSimulator_ScaleDispatchInfo, reinterpret_cast<void*>(&ScaleDispatchInfo)},
        {addr::cSC4ProtectionSimulator_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4ProtectionSimulator_Read, reinterpret_cast<void*>(&Read)},
        {addr::SC4_CopyOccupantList, reinterpret_cast<void*>(&CopyOccupantList)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) == sizeof(originals) / sizeof(originals[0]));
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
    "simulators.protection.base",
    "simulators/protection",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ProtectionSimulator_DoMessage,
    "Our fire and police base: messages, the coverage walk, stations, units, Init, Shutdown, Write, Read (docs/fire-police.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::protection_sim
