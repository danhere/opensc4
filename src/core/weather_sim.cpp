// Our cSC4WeatherSimulator (build 641), first part: its interface, swapped
// into the original object's vtable (docs/weather.md). The weather grid is
// coarser than the city's (cell sizes +0x34, +0x38); the getters read the
// moisture map (+0xDC), the manual moisture map (+0xE8), humidity (+0xF8)
// and the wind (+0x124 to +0x12C).
#include "core/weather_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::weather_sim {
namespace {


using SetLengthFn = void(__cdecl*)(float* vector, float length);
using RecomputeFn = void(__thiscall*)(void* self, const int32_t* rect);

// value - 0.5 rounded, then one more when value is exactly that plus one:
// the game's floor for the moisture map's cells.
int32_t FloorThroughFloat(double value)
{
    int32_t cell = RoundThroughFloat(value - Global<float>(addr::SC4_FloatHalf));
    if (static_cast<double>(cell + 1) == value)
        cell += 1;
    return cell;
}

uint8_t* const* Rows(void* self, std::size_t vector)
{
    return *reinterpret_cast<uint8_t* const* const*>(static_cast<char*>(self) + vector);
}

// 6, 7 (0x00477850, 0x00477860, shared).
float __fastcall CellWidthX(void* self, void*) { return At<float>(self, field::kCellWidthX); }
float __fastcall CellWidthZ(void* self, void*) { return At<float>(self, field::kCellWidthZ); }

// 8, 9 (0x004ABAD0, 0x006AAC50, shared one-field getters).
int32_t __fastcall CellCountX(void* self, void*) { return At<int32_t>(self, field::kCellsX); }
int32_t __fastcall CellCountZ(void* self, void*) { return At<int32_t>(self, field::kCellsZ); }

// 10 (0x007255C0).
void __fastcall PositionToCell(void* self, void*, float x, float z, int32_t* cellX, int32_t* cellZ)
{
    *cellX = Ftol(static_cast<double>(x) / At<float>(self, field::kCellWidthX));
    *cellZ = Ftol(static_cast<double>(z) / At<float>(self, field::kCellWidthZ));
}

// 11 (0x007255F0).
void __fastcall CellToPosition(void* self, void*, int32_t cellX, int32_t cellZ, float* x, float* z)
{
    *x = static_cast<float>(static_cast<double>(cellX) * At<float>(self, field::kCellWidthX));
    *z = static_cast<float>(static_cast<double>(cellZ) * At<float>(self, field::kCellWidthZ));
}

// 12 (0x00725610): both from x, as the game does.
void __fastcall CellToStandardCityCell(void* self, void*, int32_t cellX, int32_t, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, field::kCityCellsX) * cellX;
    *z = At<int32_t>(self, field::kCityCellsZ) * cellX;
}

// 13 (0x00725640).
void __fastcall StandardCityCellToCell(void* self, void*, int32_t x, int32_t z, int32_t* cellX, int32_t* cellZ)
{
    *cellX = x / At<int32_t>(self, field::kCityCellsX);
    *cellZ = z / At<int32_t>(self, field::kCityCellsZ);
}

// 14 (0x00725660): (month - 1) / 3 of slot 15.
int32_t __fastcall GetCurrentSeason(void* self, void*)
{
    return (Virtual<int32_t>(self, 0x3C) - 1) / 3;
}

// 15 (0x00725680): the month (simulator date, slot 23), shifted by six in
// the southern hemisphere (city slot 30 not 1).
int32_t __fastcall GetCurrentSeasonMonth(void* self, void*)
{
    const int32_t hemisphere = Virtual<int32_t>(At<void*>(self, field::kCity), 0x78);
    const int32_t month = Virtual<int32_t>(Virtual<void*>(At<void*>(self, field::kSimulator), 0x1C), 0x5C);
    return hemisphere == 1 ? month : (month + 5) % 12 + 1;
}

// 16 (0x00727F90): a city-cell rectangle in weather cells (16 city cells
// each, rounded out), then the moisture recompute over it.
void __fastcall UpdateMoisture(void* self, void*, const int32_t* rect)
{
    if (!At<void*>(self, field::kTerrain))
        return;
    const int32_t cells[4] = {rect[0] / 16, rect[1] / 16, (rect[2] + 15) / 16, (rect[3] + 15) / 16};
    reinterpret_cast<RecomputeFn>(addr::cSC4WeatherSimulator_RecomputeMoisture)(self, cells);
}

// 17 (0x00726150): the moisture map at a position, clamped into the city.
uint32_t __fastcall GetMoistureValue(void* self, void*, float x, float z)
{
    if (!At<uint8_t>(self, 0x10))
        return 0;
    const float one = Global<float>(addr::SC4_FloatOneA81228);
    if (x < 0.0f)
        x = 0.0f;
    else if (!(x < At<float>(self, field::kSizeX)))
        x = static_cast<float>(static_cast<double>(At<float>(self, field::kSizeX)) - one);
    double zz = z;
    if (z < 0.0f)
        zz = 0.0;
    else if (!(z < At<float>(self, field::kSizeZ)))
        zz = static_cast<double>(At<float>(self, field::kSizeZ)) - one;
    const int32_t cellX = FloorThroughFloat(static_cast<double>(x) * At<float>(self, field::kInvCellWidthX));
    const int32_t cellZ = FloorThroughFloat(zz * At<float>(self, field::kInvCellWidthZ));
    return Rows(self, field::kMoisture)[3 * cellX][cellZ];
}

// 18 (0x007256C0).
void __fastcall GetMoistureValueMapPtr(void* self, void*, void** map)
{
    *map = static_cast<char*>(self) + field::kMoisture;
}

// 19 (0x007256D0): slot 17, high four bits.
uint32_t __fastcall GetMoistureType(void* self, void*, float x, float z)
{
    return Virtual<uint32_t>(self, 0x44, x, z) >> 4;
}

// 20 (0x00726250): a rectangle (meters) of the manual moisture map set to
// the value (0 to 255) times 1.275, rounded.
void __fastcall SetManualMoistureAddition(void* self, void*, float x1, float z1, float x2, float z2, float value)
{
    double clamped = value;
    if (value < 0.0f)
        clamped = 0.0;
    else if (!(value <= Global<float>(addr::SC4_Float255)))
        clamped = Global<float>(addr::SC4_Float255);
    const uint8_t amount = static_cast<uint8_t>(RoundThroughFloat(clamped * Global<float>(addr::WeatherTuning_ManualScale)));
    int32_t cellX1 = Ftol(static_cast<double>(x1) * At<float>(self, field::kInvCellWidthX));
    int32_t cellZ1 = Ftol(static_cast<double>(z1) * At<float>(self, field::kInvCellWidthZ));
    int32_t cellX2 = Ftol(static_cast<double>(x2) * At<float>(self, field::kInvCellWidthX));
    int32_t cellZ2 = Ftol(static_cast<double>(z2) * At<float>(self, field::kInvCellWidthZ));
    if (cellX1 < 0)
        cellX1 = 0;
    else if (cellX2 >= At<int32_t>(self, field::kCellsX))
        cellX2 = At<int32_t>(self, field::kCellsX) - 1;
    if (cellZ1 < 0)
        cellZ1 = 0;
    else if (cellZ2 >= At<int32_t>(self, field::kCellsZ))
        cellZ2 = At<int32_t>(self, field::kCellsZ) - 1;
    for (int32_t x = cellX1; x < cellX2; ++x)
        for (int32_t z = cellZ1; z < cellZ2; ++z)
            Rows(self, field::kManualMoisture)[3 * x][z] = amount;
}

// 21 (0x007256F0): (1 - h) * 0.5 + h, unrounded.
double __fastcall GetHumidity(void* self, void*, int32_t, int32_t)
{
    const double humidity = At<float>(self, field::kHumidity);
    return (static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)) - humidity) * Global<float>(addr::SC4_FloatHalf) +
           humidity;
}

// 22 (0x00725A00): the wind speed; the direction scaled to it (0x005D76D0).
float __fastcall GetAmbientWind(void* self, void*, float* direction)
{
    const float speed = At<float>(self, field::kWindSpeed);
    if (direction) {
        direction[0] = At<float>(self, field::kWindDirection);
        direction[1] = At<float>(self, field::kWindDirection + 4);
        reinterpret_cast<SetLengthFn>(addr::SC4Vector2_SetLength)(direction, speed);
    }
    return speed;
}

// 23 (0x00725710): the same everywhere (slot 22).
double __fastcall GetWindAtCell(void* self, void*, int32_t, int32_t, float* direction)
{
    return Virtual<float>(self, 0x58, direction);
}

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(6, 0x00477850, CellWidthX),
    OPENSC4_SLOT(7, 0x00477860, CellWidthZ),
    OPENSC4_SLOT(8, 0x004ABAD0, CellCountX),
    OPENSC4_SLOT(9, 0x006AAC50, CellCountZ),
    OPENSC4_SLOT(10, 0x007255C0, PositionToCell),
    OPENSC4_SLOT(11, 0x007255F0, CellToPosition),
    OPENSC4_SLOT(12, 0x00725610, CellToStandardCityCell),
    OPENSC4_SLOT(13, 0x00725640, StandardCityCellToCell),
    OPENSC4_SLOT(14, 0x00725660, GetCurrentSeason),
    OPENSC4_SLOT(15, 0x00725680, GetCurrentSeasonMonth),
    OPENSC4_SLOT(16, 0x00727F90, UpdateMoisture),
    OPENSC4_SLOT(17, 0x00726150, GetMoistureValue),
    OPENSC4_SLOT(18, 0x007256C0, GetMoistureValueMapPtr),
    OPENSC4_SLOT(19, 0x007256D0, GetMoistureType),
    OPENSC4_SLOT(20, 0x00726250, SetManualMoistureAddition),
    OPENSC4_SLOT(21, 0x007256F0, GetHumidity),
    OPENSC4_SLOT(22, 0x00725A00, GetAmbientWind),
    OPENSC4_SLOT(23, 0x00725710, GetWindAtCell),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 25;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4WeatherSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("weather interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("weather interface: %u of %d cISC4WeatherSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.weather.interface",
    "simulators/weather",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_vftable,
    "Our cISC4WeatherSimulator getters on the original object (docs/weather.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::weather_sim
