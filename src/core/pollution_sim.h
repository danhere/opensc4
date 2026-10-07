// cSC4PollutionSimulator (build 641) as our reimplementation sees it: the
// original object's fields and the game interfaces it calls, shared by the
// replacements of its parts (docs/pollution.md).
#pragma once

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::pollution_sim {

// Fields of cSC4PollutionSimulator (build 641) used here; offsets from the
// object's start (its cISC4PollutionSimulator vtable pointer).
namespace field {
constexpr std::size_t kZoneManager = 0x24;     // cISC4ZoneManager*
constexpr std::size_t kValueGrids = 0x40;      // cISC4SimGrid<short>*[4], indexed by pollution type
constexpr std::size_t kRadiationGrid = 0x4C;   // cISC4SimGrid<short>*, also kValueGrids[3]
constexpr std::size_t kAirGrid = 0x50;         // cISC4SimGrid<short>*; GetPollutionGrid(0)
constexpr std::size_t kWaterGrid = 0x54;       // GetPollutionGrid(1)
constexpr std::size_t kGarbageGrid = 0x58;     // GetPollutionGrid(2); bit 15 marks an active landfill tile
constexpr std::size_t kWaterTreatmentList = 0x68; // circular lists of occupants, sentinel node pointers
constexpr std::size_t kIncineratorList = 0x6C;
constexpr std::size_t kWasteToEnergyList = 0x70;
constexpr std::size_t kRecyclingList = 0x74;
constexpr std::size_t kAverageAir = 0x100;     // int32
constexpr std::size_t kAverageWater = 0x104;   // int32
constexpr std::size_t kAverageGarbage = 0x128; // uint16
constexpr std::size_t kMonthlyIncineratorCapacity = 0x12C; // int32, slot 31
constexpr std::size_t kMonthlyWasteToEnergyCapacity = 0x130; // int32, slot 32
constexpr std::size_t kRecyclingCenterEffect = 0x134; // float; slot 33 returns its address
constexpr std::size_t kGarbageUncollected = 0x138;   // bool
constexpr std::size_t kTotalGarbageUncollected = 0x13C; // int32
constexpr std::size_t kLandfilledThisMonth = 0x170; // int64
constexpr std::size_t kProducedLastMonth = 0x178;   // int64 each, copied from this month's figures on MonthPassed
constexpr std::size_t kImportedLastMonth = 0x180;
constexpr std::size_t kExportedLastMonth = 0x188;
constexpr std::size_t kRecycledLastMonth = 0x190;   // double
constexpr std::size_t kIncineratedLastMonth = 0x198;
constexpr std::size_t kConvertedLastMonth = 0x1A0;
constexpr std::size_t kLandfilledLastMonth = 0x1A8;
constexpr std::size_t kAbductedPrevMonth = 0x1B8;   // uint32
constexpr std::size_t kActiveLandfillTiles = 0x1BC; // uint32
constexpr std::size_t kRadiationChanged = 0x208;    // bool each: set by the setters
constexpr std::size_t kAirChanged = 0x209;
constexpr std::size_t kWaterChanged = 0x20A;
constexpr std::size_t kGarbageChanged = 0x20B;
constexpr std::size_t kWorstAirTract = 0x210;     // uint16 x, uint16 z, int32 value
constexpr std::size_t kWorstWaterTract = 0x218;
constexpr std::size_t kWorstGarbageTract = 0x220;
constexpr std::size_t kGarbageSupply = 0x22C;     // int32
constexpr std::size_t kGarbageCapacity = 0x230;   // int32
} // namespace field

template <typename T>
inline T& At(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

template <typename T>
inline T& Global(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}

// Calls the virtual function at byte offset `offset` of `object`'s vtable.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t offset, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    const auto fn = reinterpret_cast<Fn>((*static_cast<void***>(object))[offset / sizeof(void*)]);
    return fn(object, args...);
}

// cISC4SimGrid<short> (gzcom-dll slots), as the game's code calls them.
// GetCellValue returns the value in AX; the rest of EAX is the callee's.
inline int16_t GetCellValue(void* grid, uint32_t x, uint32_t z) { return Virtual<int16_t>(grid, 0x1C, x, z); }
inline uint32_t GetTractShift(void* grid) { return Virtual<uint32_t>(grid, 0x2C); }
inline uint32_t SetTractValue(void* grid, uint32_t x, uint32_t z, int16_t value)
{
    return Virtual<uint32_t>(grid, 0x68, x, z, value);
}

inline void* Grid(void* self, std::size_t offset) { return At<void*>(self, offset); }

// cISC4Occupant::AsPropertyHolder (slot 6).
inline void* PropertyHolder(void* occupant) { return Virtual<void*>(occupant, 0x18); }

// Game helpers: read a property through a property holder (cdecl).
using GetPropertyFn = uint32_t(__cdecl*)(void* holder, uint32_t property, void* value);
inline const auto GetPropertySint32 = reinterpret_cast<GetPropertyFn>(addr::SC4_GetPropertySint32);
inline const auto GetPropertyUint32 = reinterpret_cast<GetPropertyFn>(addr::SC4_GetPropertyUint32);
inline const auto GetPropertyPercentage = reinterpret_cast<GetPropertyFn>(addr::SC4_GetPropertyPercentage);

constexpr uint32_t kZoneLandfill = 14; // cISC4ZoneManager::ZoneType::Landfill

// cISC4PollutionSimulator slots that our functions call back through the
// object's vtable, as the game's do.
constexpr std::size_t kSlotGetAirValue = 0x1C;
constexpr std::size_t kSlotGetWaterValue = 0x20;
constexpr std::size_t kSlotGetGarbageValue = 0x24;
constexpr std::size_t kSlotIsActiveLandfill = 0xC4;
constexpr std::size_t kSlotGetMaxCapacity = 0xD0;
constexpr std::size_t kSlotGetCurrentCapacity = 0xD4;

// The game's lists: circular, a sentinel node; nodes are {next, prev, object}.
inline void* NextNode(void* node) { return *static_cast<void**>(node); }
inline void* NodeObject(void* node) { return static_cast<void**>(node)[2]; }

} // namespace opensc4::pollution_sim
