// Shared between the four parts of our cSC4LotDeveloper (build 641): the
// object part (src/core/lot_object.cpp: the object, Init, Shutdown, the save
// record, DoMessage and the small slots below 0x006C4EF0), the terrain part
// (lot_terrain.cpp, 0x006C4EF0-0x006C7280), the build part (lot_build.cpp,
// 0x006C7280-0x006CB4D0) and the construct part (lot_construct.cpp, from
// 0x006CB4D0). The lot developer builds lots: foundations and retaining
// walls, levelling the ground, base textures, the props of a lot,
// construction and demolition, travel designations.
//
// Class map. Class ID 0xA990BFE0; one object per city (the city's slot 79,
// +0x13C; global 0x00B43D20; the building development simulator's slot 36
// returns it too). 0x108 bytes; factory 0x006C8DF0 (operator new, then a
// jump to the constructor 0x006C8650, its only caller); destructor
// 0x006C7280, deleting destructor 0x006C8810. Code 0x006C3B00 to 0x006CDEED
// (0x006C3AF0 is the last thunk of the class before; 0x006CDEF0 starts the
// neighbours simulator).
//   +0x0 primary cISC4LotDeveloper (IID 0x4818E5D2), vtable 0x00AB1B98, 66
//        slots: 0 QueryInterface 0x006C3B80, 1 AddRef 0x004F7BA0 (shared), 2
//        Release 0x006C49D0, 3 Init 0x006CB9A0, 4 Shutdown 0x006C7380, 5
//        GetSimulatorType 0x006C3C40 (0x6818E5C9), 6-64 the developer's
//        slots, 65 the deleting destructor.
//   +0x4 cIGZMessageTarget2 (IID 0x452294AA), vtable 0x00AB1B88, 4 slots:
//        thunks 0x006C7340 / 0x0068D230 (shared) / 0x006C7360, DoMessage
//        0x006CD940 (`this` = object + 4).
//   +0x8 cIGZSerializable (IID 0xE4FDA3D4), vtable 0x00AB1B70, 6 slots:
//        thunks 0x006C7350 / 0x0068D250 (shared) / 0x006C7370, Write
//        0x006C66F0, Read 0x006CD120 (`this` = object + 8), GetGZCLSID
//        0x006C3B00.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::lot_sim {

// Fields of cSC4LotDeveloper (offsets from the object's start). Names are
// ours. Evidence: the constructor, Init 0x006CB9A0 (which global each
// service comes from), the tuning load 0x006C4A20, Write/Read, DoMessage.
namespace field {
constexpr std::size_t kInitialized = 0xC;   // uint8: Init sets it last, Shutdown clears it first
constexpr std::size_t kRefCount = 0x10;     // int32 (signed compare in Release)
constexpr std::size_t kMessages = 0x14;     // the message server (0x00B43CCC)
constexpr std::size_t kDevelopment = 0x18;  // the building development simulator (0x00B43D68, vtable 0x00AB0AF8)
constexpr std::size_t kCity = 0x1C;         // the city (0x00B43CEC)
constexpr std::size_t kService20 = 0x20;    // 0x00B43D00 (the city's slot 31, vtable 0x00A88004; its slot 9, ten arguments, levels terrain vertices)
constexpr std::size_t kLots = 0x24;         // the lot manager (0x00B43D08, vtable 0x00AB37C0)
constexpr std::size_t kOccupants = 0x28;    // the occupant manager (0x00B43D0C, vtable 0x00A8F7F0)
constexpr std::size_t kService2C = 0x2C;    // 0x00B43D94 (the plumbing simulator)
constexpr std::size_t kProps = 0x30;        // the prop manager (0x00B43D10, vtable 0x00A8F928)
constexpr std::size_t kPower = 0x34;        // the power simulator (0x00B43DA0)
constexpr std::size_t kService38 = 0x38;    // 0x00B43D80 (the flora simulator, the city's slot 59)
constexpr std::size_t kTerrain = 0x3C;      // the terrain (0x00B43CF4, vtable 0x00AB3D18)
constexpr std::size_t kVerticesX = 0x40;    // uint32: the terrain's slot 7 + 1, vertices in a row (GetTerrainAltitudeByVertex: z * this + x); saved
constexpr std::size_t kSizeX = 0x44;        // float: the terrain's slot 7 count * its slot 6 cell size (2048.0 on a medium city); saved
constexpr std::size_t kSizeZ = 0x48;        // float: the same with the terrain's slot 8; saved
constexpr std::size_t kCellsZ = 0x4C;       // int32: the city's slot 94 (+0x178)
constexpr std::size_t kCellsX = 0x50;       // int32: the city's slot 93 (+0x174)
constexpr std::size_t kTraffic = 0x54;      // the traffic simulator (0x00B43DA8)
constexpr std::size_t kFilter = 0x58;       // an occupant filter (0xC bytes, vtables 0x00AB1B28 / 0x00AB1B10; its test 0x006C3C20 accepts type 0x74758926), referenced
constexpr std::size_t kForceEmptyTextures = 0x5C; // uint8: GetForceEmptyLotBaseTextures (0x006AAF40) returns it; Init flips it and calls the setter (slot 47) with the old value
constexpr std::size_t kDirty5D = 0x5D;      // uint8 (1 at first): set by 0x006C4910 and by message 0x99EF1143 (0xC772BF98); the update 0x006CD770 moves it to +0xB8
constexpr std::size_t kDirty5E = 0x5E;      // uint8 (1 at first): set by RemoveLotFromItsBusinessUnit and the same message; moved to +0xB9
constexpr std::size_t kDemand5F = 0x5F;     // uint8: message 0x69247DC7 sets it when the demand (0x00B43D74 slot 6: 0x2000, 0x20000) slot 9 value is above 0; Init clears it
constexpr std::size_t kDate60 = 0x60;       // uint32: message 0x26D31EC4 sets it to the simulator's slot 8 + 31; Init clears it
constexpr std::size_t kBuildings = 0x64;    // std::deque<building occupant*> (0x28 bytes: begin iterator {cur, first, last, node} +0x64, end iterator +0x74, map +0x84, map size +0x88; 32 to a block), each referenced; saved
constexpr std::size_t kNextBuilding = 0x8C; // deque iterator (16 bytes): the next building the day/night step (0x006CBD60) takes; saved as an index
constexpr std::size_t kNightState = 0x9C;   // uint32: 0 idle, 1 or 2 while the buildings are stepped after message 0x44778234; saved as a byte
constexpr std::size_t kStepCount = 0xA0;    // uint32 (8 at first; property 0xE900EA2C): buildings per step
constexpr std::size_t kStepMs = 0xA4;       // uint32: milliseconds since the last step (200 at first, property 0xE900EA2B loads into it, Read clears it; a step at more than 200)
constexpr std::size_t kFlagA8 = 0xA8;       // uint8 (0 at first); saved as a bool
constexpr std::size_t kUpdateState = 0xAC;  // uint32: the state of the lot update 0x006CD770 (0, 1, 3); 3 at construction, Init sets 0
constexpr std::size_t kLotIterator = 0xB0;  // the lot manager's lot iterator of the update (slot 15; freed by Shutdown), or null
constexpr std::size_t kUpdateCount = 0xB4;  // int32: lots done in the current update
constexpr std::size_t kUpdateB8 = 0xB8;     // uint8: +0x5D as the update began; +0xB9 the same for +0x5E; +0xBA whether the date is past +0x60
constexpr std::size_t kCellIterator = 0xBC; // a cell iterator (0x20 bytes, operator new; rectangle = the city's cells / 4 - 1), freed by Shutdown
constexpr std::size_t kTuningC0 = 0xC0;     // uint16 (90 at first; property 0x47E2C400)
constexpr std::size_t kTuningC4 = 0xC4;     // float 0.6, +0xC8 float 0.4, +0xCC float 0.2 (property 0x47E2C401: [2], [1], [0])
constexpr std::size_t kTuningD0 = 0xD0;     // float 1.0 (property 0xE900EA24)
constexpr std::size_t kFoundationHeight = 0xD4; // float 1.0 (property 0xE900EA25): the slope limit; a lot or building needs a foundation when its slope (slots 29 to 31) is above it (the terrain and construct parts compare slopes with it too)
constexpr std::size_t kTuningD8 = 0xD8;     // uint32 0x890B7312 (property 0xE900EA26)
constexpr std::size_t kFoundationType = 0xDC; // uint32 0x890B7312 (property 0x6916516B): the foundation of buildings that name none (property 0x88FCD877)
constexpr std::size_t kTuningE0 = 0xE0;     // bool true (property 0xE900EA27); +0xE1 bool true (0xE900EA28)
constexpr std::size_t kLevelLimit = 0xE4;   // float 0 (property 0xE900EA29): LevelAreaInternalVertices skips vertices not more than this above the target
constexpr std::size_t kLevelDefault = 0xE8; // float 0 (property 0xE900EA2A): AttemptAreaLevelling's limit when the caller passes the "unset" value at 0x00AB1B00
constexpr std::size_t kCurve = 0xEC;        // SC4Curve (16 bytes; property 0x2A2D7824)
constexpr std::size_t kTree = 0xFC;         // red-black tree (head +0xFC, size +0x100; nodes without owned values), cleared by Shutdown
constexpr std::size_t kSize = 0x108;
} // namespace field

// Logs a part's first call once ("lot: first <name>"; defined in
// lot_object.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::lot_sim
