// Shared between the parts of our cSC4Lot (build 641): the class map, every
// vtable slot by slot and the object's fields. A lot is one parcel of the
// city: its cell rectangle, facing, zone type, state and condition, its
// building type, capacity, populations and travel populations, travel jobs,
// power, road and job access, and its business unit.
//
// The parts: citylot_object.cpp (the object, identity, Init, Shutdown,
// PostCityInit, the save record and the functions with their entry in
// 0x006BF180-0x006C09E0), citylot_capacity.cpp (0x006C09E0-0x006C2340) and
// citylot_state.cpp (0x006C2340 to the end, 0x006C3AF8).
//
// Class map
//   class ID 0xC9BD5D4A (GetGZCLSID 0x006BF180), cISC4Lot IID 0xE818FCED
//   0x8C bytes, from the pool at 0x00B0D3AC (blocks of 0x74 lots, block
//   header at address & 0xFFFFC000: free list +0x8, used count +0xC;
//   cSC4Lot::Allocate 0x006BF430; the deleting destructor 0x006C3000 gives
//   the memory back to the pool)
//   constructors: 0x006C1E50 (x, z, width, depth, facing; the lot manager's
//   CreateLot), 0x006C2340 (default; the factory 0x006C2FC0, used when a
//   save is read), 0x006C2450 (copy)
//   destructor 0x006C25D0, deleting destructor 0x006C3000 (reached only
//   through the holder part's slot 22, thunk 0x006C22A0)
//   QueryInterface 0x006BF190: IID 1 and 0xE818FCED give the object,
//   0xE4FDA3D4 (cIGZSerializable) gives +0x4; the parts +0x8 and +0x1C are
//   only reached through slots 3 and 4. AddRef 0x004664A0 (shared: ++ at
//   +0xC), Release 0x006BF460 (at the last reference: Shutdown, then the
//   holder part's slot 22 with 1).
//
// +0x00 primary cISC4Lot, vtable 0x00AB19B0, 70 slots (byte offset, stack
// arguments from the callee's ret N, function):
//    0 +000 2 QueryInterface 0x006BF190       35 +08C 0 GetCondition 0x006C2110
//    1 +004 0 AddRef 0x004664A0               36 +090 1 SetCondition 0x006C26D0
//    2 +008 0 Release 0x006BF460              37 +094 0 IsInLivableState 0x006C2150
//    3 +00C 0 AsPropertyHolder 0x006C2040     38 +098 0 HasCapacity 0x006C2160
//    4 +010 0 AsTrafficSource 0x006C2050      39 +09C 0 IsInDemolishableState 0x006C2180
//    5 +014 0 Init 0x006BF490                 40 +0A0 0 GetDaysSinceLastStateChange 0x006BF630
//    6 +018 0 IsInitialized 0x006C2020        41 +0A4 0 GetOccupantWealth 0x006C21F0
//    7 +01C 0 Shutdown 0x006C3060             42 +0A8 0 GetLandValue 0x006BFC60 (EDX:EAX)
//    8 +020 0 PostCityInit 0x006BF200         43 +0AC 0 IsHistorical 0x006C2220
//    9 +024 0 GetLotConfiguration 0x00A6CAB0  44 +0B0 1 SetHistorical 0x006C2230
//   10 +028 1 SetLotConfiguration 0x0060CB80  45 +0B4 0 GetVacateReason 0x006C2250
//   11 +02C 1 GetLotConfigurationObject 0x006BFEA0
//   12 +030 0 GetFlags 0x006C2030             46 +0B8 1 GetBuildingType 0x006BFA00
//   13 +034 1 SetFlags 0x006BF220             47 +0BC 1 SetBuildingType 0x006C2200
//   14 +038 1 GetBoundingRect 0x006BF320      48 +0C0 0 GetBuilding 0x006BFA30
//   15 +03C 1 GetCenter 0x006BF560            49 +0C4 1 GetLotOccupants 0x006BFF70
//   16 +040 0 GetAltitude 0x006C2060 (ST0)    50 +0C8 2 GetCapacity(type, force) 0x006C05A0
//   17 +044 1 SetAltitude 0x006BF5C0          51 +0CC 1 GetCapacity(wealth) 0x006BF9B0
//   18 +048 0 GetSlope 0x006C2070 (ST0)       52 +0D0 2 GetAvailableCapacity 0x006C0640
//   19 +04C 0 GetSlopeAtTimeOfConstruction 0x006C2080 (ST0)
//   20 +050 0 UpdateSlope 0x006BF610          53 +0D4 1 GetPopulation 0x006C04A0
//   21 +054 2 GetLocation 0x006BF2E0          54 +0D8 0 GetPopulations 0x006C21E0
//   22 +058 2 GetSize 0x006BF300              55 +0DC 2 SetPopulation 0x006C1940
//   23 +05C 0 GetFacing 0x006C2090            56 +0E0 1 GetTravelPopulation 0x006C0510
//   24 +060 1 SetFacing 0x006C20A0            57 +0E4 0 ResetTravelPopulation 0x006C0550
//   25 +064 0 GetUnit 0x0068D1C0              58 +0E8 1 AddTravelPopulation 0x006C0BA0
//   26 +068 1 SetUnit 0x006C20B0              59 +0EC 0 GetTravelDesignate 0x0081F910
//   27 +06C 0 HasJobAccess 0x006BFCE0         60 +0F0 1 SetTravelDesignate 0x006BF660
//   28 +070 0 HasRoadAccess 0x006C20E0        61 +0F4 1 GetOccupancy 0x006C0CF0
//   29 +074 1 HasOrganicRoadAccess 0x006C1D10 62 +0F8 1 GetOccupancyByType 0x006C10F0
//   30 +078 0 UpdateRoadAccess 0x006C1DA0     63 +0FC 1 GetTravelJobs 0x006BF350 (ST0)
//   31 +07C 0 GetZoneType 0x006C20F0          64 +100 1 GetJobs 0x006BF800
//   32 +080 0 UpdateZoneType 0x006BFC40       65 +104 0 GetNeedsPower 0x006C21A0
//   33 +084 0 GetState 0x006C2100             66 +108 0 GetHasPower 0x006C21B0
//   34 +088 3 SetState 0x006C3100             67 +10C 1 SetHasPower 0x006C21C0
//                                             68 +110 0 GetVerticesAreLocked 0x006C2120
//                                             69 +114 1 SetVerticesAreLocked 0x006C2130
//   (slots 9, 10, 25 and 59 are code shared with other classes; gzcom-dll's
//   cISC4Lot.h has the order right, with slot 50 the two-argument
//   GetCapacity and slot 51 the one-argument one.)
// +0x04 cIGZSerializable, vtable 0x00AB1994, 6 slots: 0 QueryInterface
//   thunk 0x006C2260, 1 AddRef thunk 0x006C2270, 2 Release thunk
//   0x006C2300, 3 Write 0x006C0080 (1), 4 Read 0x006C35F0 (1), 5 GetGZCLSID
//   0x006BF180. Write and Read get `this` = lot + 4.
// +0x08 the property holder (cISCPropertyHolder; base constructed by
//   0x005FF100, 0x14 bytes), vtable 0x00AB1930, 25 slots (0x00AB1994
//   follows it): 0-2 thunks 0x006C2280/0x006C2290/0x006C2310, 3-21 the
//   shared holder code (0x005FEB20 ...), 22 the deleting destructor thunk
//   0x006C22A0 (1 argument; to 0x006C3000 with this - 8), 23 Write thunk
//   0x006C2330 and 24 Read thunk 0x006C3AF0 (both this - 4, then a direct
//   jump: the holder's own save slots lead to the lot's).
// +0x1C cISC4TrafficSource, vtable 0x00AB1914, 6 slots: 0-2 thunks
//   0x006C22B0/0x006C22C0/0x006C2320, 3 0x0080BC20 (the part + 0x68 = lot +
//   0x84, the trip list), 4 0x006C27E0 (0 arguments), 5 0x006C22D0 (1: the
//   cell rectangle as four int32 from the bytes at part + 4 = lot + 0x20).
//
// The business unit (cSC4BusinessUnit, class ID 0x4A232DA8, 0x20 bytes,
// IID 0x6A233069) lives in the same code: primary vtable 0x00AB18DC (0
// QueryInterface 0x006BF390, 1 AddRef 0x004664A0, 2 Release 0x006BFE40, 3
// Add 0x006C0940 (1), 4 Remove 0x006C08B0 (1), 5 GetMembers 0x008085F0),
// +0x4 cIGZSerializable 0x00AB18C4 (Write 0x006C0370), +0x8 a property
// holder 0x00AB1860, +0xC reference count, +0x1C list of member lots (head;
// nodes: next, previous, lot (referenced)).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::citylot_sim {

constexpr uint32_t kClassId = 0xC9BD5D4A;
constexpr uint32_t kIidLot = 0xE818FCED;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidBusinessUnit = 0x6A233069;
constexpr std::size_t kObjectSize = 0x8C;

// Fields of cSC4Lot (offsets from the object's start). Names are ours.
// Evidence: the three constructors, Write 0x006C0080 / Read 0x006C35F0
// (which read 4 less: they get lot + 4) and the getters named.
namespace field {
constexpr std::size_t kSerializable = 0x4;   // cIGZSerializable part
constexpr std::size_t kHolder = 0x8;         // property holder part (0x14 bytes; saved through 0x005FD990 / 0x005FF280)
constexpr std::size_t kRefCount = 0xC;       // uint32 (the holder base's count; AddRef 0x004664A0, Release 0x006BF460)
constexpr std::size_t kTrafficSource = 0x1C; // cISC4TrafficSource part
constexpr std::size_t kMinX = 0x20;          // uint8 cell rectangle: min x, min z, max x, max z (GetBoundingRect); saved
constexpr std::size_t kMinZ = 0x21;
constexpr std::size_t kMaxX = 0x22;
constexpr std::size_t kMaxZ = 0x23;
constexpr std::size_t kAltitude = 0x24;      // float (slots 16, 17); saved
constexpr std::size_t kSlope = 0x28;         // float (slots 18, 20); saved third of the three floats
constexpr std::size_t kBuiltSlope = 0x2C;    // float: slope at the time of construction (slot 19); saved second
constexpr std::size_t kConfigId = 0x30;      // uint32: the lot configuration's ID as read from a save; Init resolves it into +0x34 and zeroes it
constexpr std::size_t kConfig = 0x34;        // cISC4LotConfiguration*, referenced (slots 9, 10); its ID is saved
constexpr std::size_t kStateDay = 0x38;      // uint32: the day number of the last state change, 0 = never (slot 40); saved
constexpr std::size_t kBuildingType = 0x3C;  // uint32 (slots 46, 47); saved
constexpr std::size_t kMap40 = 0x40;         // a tree (head, size +0x44, 12 bytes); not saved; nodes of 0x18 bytes hold {int32 key, uint8} (0x006C03E0, 0x006BFE60)
constexpr std::size_t kUnit = 0x4C;          // cISC4BusinessUnit*, referenced by SetUnit (slots 25, 26); saved as a reference
constexpr std::size_t kDesignate = 0x50;     // cISC4Lot*: the travel designate, referenced (slots 59, 60); saved as a reference
constexpr std::size_t kPopulations = 0x54;   // vector<map<uint32 developer type, uint16>> (begin, end +0x58, capacity +0x5C; 12-byte maps: head, size, -); slot 54 returns its address; saved
constexpr std::size_t kTravel = 0x60;        // map<uint32 developer type, uint16>: travel population (head, size +0x64); saved
constexpr std::size_t kTravelJobs = 0x6C;    // float[3]: travel jobs of wealth 1, 2, 3 (slot 63); saved
constexpr std::size_t kInitialized = 0x78;   // uint8 (slot 6); Read sets it before anything else
constexpr std::size_t kFlags = 0x79;         // uint8 (slots 12, 13): 0x01 marks the lot's cells in the terrain picker's grid (SetFlags), 0x08 needs power (65), 0x10 has power (66), 0x20 historical (43), 0x40 vertices locked (68); saved
constexpr std::size_t kCellX = 0x7A;         // uint8: the lot's location, its front left cell by facing (slot 21); saved
constexpr std::size_t kCellZ = 0x7B;
constexpr std::size_t kWidth = 0x7C;         // uint8 size across and deep, as given to the constructor (slot 22); saved
constexpr std::size_t kDepth = 0x7D;
constexpr std::size_t kFacing = 0x7E;        // uint8 0-3 (slots 23, 24); saved
constexpr std::size_t kState = 0x7F;         // uint8 habitation state 0-6 (slot 33; 3 occupied, 2-3 has capacity); saved
constexpr std::size_t kCondition = 0x80;     // uint8 (slot 35); saved
constexpr std::size_t kZoneType = 0x81;      // uint8: the zone manager's type at the location cell (slots 31, 32); saved
constexpr std::size_t kWealth = 0x82;        // uint8 occupant wealth (slot 41); SetBuildingType zeroes it; saved
constexpr std::size_t kVacateReason = 0x83;  // uint8 (slot 45); saved
constexpr std::size_t kTrips = 0x84;         // list head (nodes of 0x34 bytes: next, previous, a 0x2C-byte trip record saved by 0x0070E0B0 / read by 0x0070E160); saved since version 7
constexpr std::size_t kRandom = 0x88;        // uint8: 0xFF until Init draws 0-99 from the generator 0x00B49120; saved since version 8
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(lot, slot::k...).
namespace slot {
constexpr std::size_t kAsPropertyHolder = 0x0C, kAsTrafficSource = 0x10, kInit = 0x14, kIsInitialized = 0x18;
constexpr std::size_t kShutdown = 0x1C, kPostCityInit = 0x20, kGetLotConfiguration = 0x24;
constexpr std::size_t kSetLotConfiguration = 0x28, kGetLotConfigurationObject = 0x2C, kGetFlags = 0x30;
constexpr std::size_t kSetFlags = 0x34, kGetBoundingRect = 0x38, kGetCenter = 0x3C, kGetAltitude = 0x40;
constexpr std::size_t kSetAltitude = 0x44, kGetSlope = 0x48, kGetBuiltSlope = 0x4C, kUpdateSlope = 0x50;
constexpr std::size_t kGetLocation = 0x54, kGetSize = 0x58, kGetFacing = 0x5C, kSetFacing = 0x60, kGetUnit = 0x64;
constexpr std::size_t kSetUnit = 0x68, kHasJobAccess = 0x6C, kHasRoadAccess = 0x70, kHasOrganicRoadAccess = 0x74;
constexpr std::size_t kUpdateRoadAccess = 0x78, kGetZoneType = 0x7C, kUpdateZoneType = 0x80, kGetState = 0x84;
constexpr std::size_t kSetState = 0x88, kGetCondition = 0x8C, kSetCondition = 0x90, kIsInLivableState = 0x94;
constexpr std::size_t kHasCapacity = 0x98, kIsInDemolishableState = 0x9C, kGetDaysSinceLastStateChange = 0xA0;
constexpr std::size_t kGetOccupantWealth = 0xA4, kGetLandValue = 0xA8, kIsHistorical = 0xAC, kSetHistorical = 0xB0;
constexpr std::size_t kGetVacateReason = 0xB4, kGetBuildingType = 0xB8, kSetBuildingType = 0xBC, kGetBuilding = 0xC0;
constexpr std::size_t kGetLotOccupants = 0xC4, kGetCapacityByType = 0xC8, kGetCapacityByWealth = 0xCC;
constexpr std::size_t kGetAvailableCapacity = 0xD0, kGetPopulation = 0xD4, kGetPopulations = 0xD8;
constexpr std::size_t kSetPopulation = 0xDC, kGetTravelPopulation = 0xE0, kResetTravelPopulation = 0xE4;
constexpr std::size_t kAddTravelPopulation = 0xE8, kGetTravelDesignate = 0xEC, kSetTravelDesignate = 0xF0;
constexpr std::size_t kGetOccupancy = 0xF4, kGetOccupancyByType = 0xF8, kGetTravelJobs = 0xFC, kGetJobs = 0x100;
constexpr std::size_t kGetNeedsPower = 0x104, kGetHasPower = 0x108, kSetHasPower = 0x10C;
constexpr std::size_t kGetVerticesAreLocked = 0x110, kSetVerticesAreLocked = 0x114;
} // namespace slot

// The game's red-black trees (STLport): a tree is {head, size, -}; the head
// node has +0x4 the root, +0x8 the first node, +0xC the last; a node has
// +0x0 color, +0x4 parent, +0x8 left, +0xC right, +0x10 the value. An empty
// tree's head points at itself (+0x8, +0xC) with a null root.
inline char* TreeNext(char* node)
{
    if (char* x = At<char*>(node, 0xC)) {
        while (char* const left = At<char*>(x, 0x8))
            x = left;
        return x;
    }
    char* y = At<char*>(node, 0x4);
    while (node == At<char*>(y, 0xC)) {
        node = y;
        y = At<char*>(y, 0x4);
    }
    if (At<char*>(node, 0xC) != y)
        node = y;
    return node;
}

// Logs a part's first call once ("citylot: first <name>"; defined in
// citylot_object.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::citylot_sim
