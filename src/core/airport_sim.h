// Shared between the parts of our cSC4AirportDeveloper and the port base it
// shares with the seaport developer (build 641). The airport developer is
// 0x64 bytes: its primary cISC4AirportDeveloper interface at +0x0 and the
// port base (cSC4PortDeveloper, 0x60 bytes) at +0x4. The base keeps the
// port types (one per exemplar, each a list of growth stages) and the ports
// (one per port lot: its type, stage, cell, efficiency and usage).
//
// airport_sim.cpp is the port base; airport_object.cpp is the airport
// developer and our own object.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::airport_sim {

// Fields of the port base (offsets from the base part, the airport
// developer + 4; constructor 0x006E9C60). Names are ours. Its interfaces:
// +0x0 cISC4PortDeveloper (19 slots), +0x4 cIGZMessageTarget2, +0x8
// cIGZSerializable.
namespace field {
constexpr std::size_t kTypesLoaded = 0xC;   // uint8: LoadTypes ran
constexpr std::size_t kInitialized = 0xD;   // uint8
constexpr std::size_t kRefCount = 0x10;     // int32
constexpr std::size_t kTypesExemplar = 0x14; // the exemplar listing the types (airport 0xCA270E9D); saved
constexpr std::size_t kTypeCount = 0x18;    // int32
constexpr std::size_t kMessages = 0x1C;     // the message server (0x00B43CCC)
constexpr std::size_t kZones = 0x20;        // the zone manager (0x00B43D14)
constexpr std::size_t kLots = 0x24;         // the lot manager (0x00B43D08)
constexpr std::size_t kCity = 0x28;         // 0x00B43CEC
constexpr std::size_t kBudget = 0x2C;       // the budget simulator (0x00B43D64)
constexpr std::size_t kDepartment = 0x30;   // the department budget (set by the subclass)
constexpr std::size_t kLotGroup = 0x34;     // uint32 asked of a removed lot (airport 0xB); low half saved
constexpr std::size_t kMessageData = 0x38;  // data 1 of the efficiency message 0x89EFA536 (airport 0x6A3E9032)
constexpr std::size_t kDepartmentId = 0x3C; // airport 0x6A4119F0
constexpr std::size_t kOccupantGroup = 0x40; // the occupant group of port buildings (-1; airport 0x1508)
constexpr std::size_t kPorts = 0x44;        // std::list<Port*>: the head node (next, previous, value)
constexpr std::size_t kTypes = 0x48;        // std::vector<PortType*> (begin, end, capacity)
constexpr std::size_t kTypeIds = 0x54;      // std::vector<uint32_t>: the types' exemplar instance IDs
} // namespace field

// A port (0x28 bytes; vtable 0x00AB2760, constructor 0x006E90E0).
namespace port {
constexpr std::size_t kStage = 0x4;      // uint32: index into the type's stages; saved
constexpr std::size_t kType = 0x8;       // PortType*; its index is saved
constexpr std::size_t kUnresolved = 0xC; // uint8: read from a save, the lot not fetched yet
constexpr std::size_t kCellX = 0xE;      // int16; saved
constexpr std::size_t kCellZ = 0x10;     // int16; saved
constexpr std::size_t kEfficiency = 0x14; // float
constexpr std::size_t kReplacing = 0x18; // uint8: its lot is being replaced by the next stage's
constexpr std::size_t kDemand = 0x1C;    // float: usage / capacity as last given to the demand
constexpr std::size_t kLot = 0x20;       // cISC4Lot*, referenced
constexpr std::size_t kUsage = 0x24;     // uint32 (the airport's UpdateUsage)
} // namespace port

// A port type (0x30 bytes; vtable 0x00AB27DC, built inline in 0x006EA740).
namespace type {
constexpr std::size_t kIndex = 0x8;       // int32 (-1 at first); saved per port as uint16
constexpr std::size_t kStageCount = 0xC;  // uint32, property 0xCA271780
constexpr std::size_t kMinEfficiency = 0x10; // float, property 0xCA271785 * 0.01
constexpr std::size_t kStages = 0x14;     // std::vector of 0x20-byte stages
constexpr std::size_t kSize = 0x20;       // uint8 (2 at first), property 0xCA271787
constexpr std::size_t kTable = 0x24;      // std::vector<std::vector<uint32_t>>, properties 0xCA271788/89/800
} // namespace type

// A growth stage of a port type (0x20 bytes; 0x006E8F10).
namespace stage {
constexpr std::size_t kIndex = 0x0;
constexpr std::size_t kBuilding = 0x4;  // property 0xCA271781: the lot configuration of the stage
constexpr std::size_t kCost = 0x8;      // int64, property 0xCA271783: the monthly cost (SumCosts)
constexpr std::size_t kCapacity = 0x10; // uint32, property 0xCA271784
constexpr std::size_t kGrowCost = 0x18; // int64, property 0xCA271786: charged when a port grows to this stage
} // namespace stage

// Logs a part's first call once (defined in airport_sim.cpp).
void FirstCall(const char* name, bool& logged);

// __ftol2 as the game uses it: truncation toward zero to 64 bits, the low
// 32 kept; the x87 "integer indefinite" (low word 0) for what does not fit.
int32_t Ftol(double value);

// The port base's functions (airport_sim.cpp), with the game's addresses.
// `self` is the base part; __fastcall with an unused second argument so the
// same functions serve as detours of the game's __thiscall ones.
namespace base {
void* __fastcall Construct(char* self, void*);                                   // 0x006E9C60
void __fastcall Destroy(char* self, void*);                                      // 0x006E9D30
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object); // 0x006E8FD0
uint32_t __fastcall Release(char* self, void*);                                  // 0x006E8F90
void* __fastcall DeleteThis(char* self, void*);                                  // 0x006E8FB0
uint8_t __fastcall Init(char* self, void*);                                      // 0x006EA950
uint8_t __fastcall Shutdown(char* self, void*);                                  // 0x006E9F60
uint8_t __fastcall DoMessage(char* target, void*, void* message);                // 0x006EA1C0 (self + 4)
uint8_t __fastcall Write(char* part, void*, void* stream);                       // 0x006E9850 (self + 8)
uint8_t __fastcall Read(char* part, void*, void* stream);                        // 0x006EAA60 (self + 8)
float* __fastcall GetAvgEfficiency(char* self, void*, float* out);               // 0x006E9900
uint8_t __fastcall GetLeastEfficientPort(char* self, void*, float* efficiency, int32_t* cell); // 0x006E9520
void __fastcall GetAllPortLots(char* self, void*, void** vector);                // 0x006EA2F0
uint32_t __fastcall GetNumPorts(char* self, void*, uint32_t sizes, uint32_t stages); // 0x006E94C0
uint32_t __fastcall GetNumPortsOfSize(char* self, void*, uint32_t size);         // 0x006E9050
void __fastcall PostAdvice(char* self, void*, uint32_t id);                      // 0x006E9120
void* __fastcall GetLot(char* port, void*);                                      // 0x006E91C0
int64_t __fastcall SumCosts(char* self, void*);                                  // 0x006E9210
uint8_t __fastcall FindByLot(char* self, void*, void* lot, char** port);         // 0x006E9590
uint8_t __fastcall GetUsageValues(char* self, void*, void* lot, uint32_t* usage, uint32_t* capacity); // 0x006E9770
uint8_t __fastcall Grow(char* self, void*);                                      // 0x006E9BD0
} // namespace base

} // namespace opensc4::airport_sim
