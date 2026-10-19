// The layout of the vehicle and pedestrian managers, part v1 (build 641;
// class IDs, vtables and functions in data/addresses/automata.toml, the
// survey in docs/automata.md, "Part v1"): cSC4VehicleManager (0xA98F9D3B,
// factory 0x005145A0, constructor 0x0055FB50, 0xAE8 bytes) and
// cSC4PedestrianManager (0xC98F9D17, factory 0x005145D0, constructor
// 0x00545500, 0xAE0 bytes), both on a base of their own (constructor
// 0x00554360, vtable 0x00A9BD88; "VehPedManagerBase" here, the name is
// ours), which derives from the base all four automata managers share
// (constructor 0x005125E0, vtable 0x00A95880; "cSC4AutomataManager").
//
// Four parts, each with its own vtable: +0x00 the manager (90 slots),
// +0x04 a four-slot interface (the vehicle manager's 0x0055B3F0 wraps the
// common base's 0x0050DD30), +0x08 the serializable part (Write, Read,
// GetGZCLSID; the factories answer this part) and +0x0C a message target.
//
// The common base's fields this part reads (its constructor zeroes them):
//   +0x14..+0x20 a float rectangle (slot 41 grows it by 48)
//   +0x34 the initialized byte
//   +0xA3D a byte that turns slot 87 on
//   +0xA44 the occupant group, +0xA8C the default class (set by the
//          derived constructors)
//   +0xA7C an object of the cells (slot 0x44 its int rectangle, slot 0x2C
//          the object of a cell)
//   +0xA84 an object whose slot 0x5C slot 87 scales
// The base's own fields (constructor 0x00554360):
//   +0xA94 the kind list (SC4PairList head, nodes (kind, 0)), +0xA98 its
//          first node (set by the derived constructors)
//   +0xA9C/+0xAA0 -1, +0xAA4 a float (0.2 vehicle, 1.0 pedestrian)
//   +0xAA8/+0xAAC/+0xAB0 a vector of 0x1C-byte records
//   +0xAB8 the record list (0x40-byte nodes: an int rectangle at +0x14 and
//          another at +0x24, a referenced object at +0x34)
//   +0xAC0 a stopwatch (SC4Stopwatch, mode 4), +0xAD8/+0xADC zero
// And the derived constructors': +0xA40 (0x16 / 0x17), +0xA48 the automaton
// class (cSC4Vehicle 0x896E714A / cSC4Pedestrian 0x896E75AF), +0xA90 (1 /
// 0); the vehicle manager's +0xAE0 zero.
//
// Part v2 reads more of the common base: +0x3C the most automata (slot 23,
// SetMaxCount), +0x44 its list of automata (0xC-byte nodes, the automaton
// at +8), +0x48 their count, +0xA3C a byte from the tuning, +0xA80 the
// script system and +0xA84 the automata controller manager (part v1's
// kScaleSource, the same field) (city slots 0xC0
// and 0xBC, set by the common base's Init). The base's Init keeps city slot
// 0x114's object at +0xA88. The vehicle manager's +0xAE0 is an object with
// a map overlay (0x00562D60 stops it). The serializable part (+8) reaches
// +0xA8C (saved) and the kind list: +0xA98 is the current kind's node, the
// list's head when there is none (Read finds the saved kind there).
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_vehmgr {

namespace field {
inline constexpr std::size_t kVtable = 0x00, kVtable4 = 0x04, kSerializable = 0x08, kMessage = 0x0C;
inline constexpr std::size_t kRect = 0x14; // four floats, the common base's
inline constexpr std::size_t kInitialized = 0x34;
inline constexpr std::size_t kScaleOn = 0xA3D;
inline constexpr std::size_t kA40 = 0xA40, kGroup = 0xA44, kAutomatonClass = 0xA48;
inline constexpr std::size_t kCells = 0xA7C, kScaleSource = 0xA84;
inline constexpr std::size_t kDefaultClass = 0xA8C, kA90 = 0xA90;
inline constexpr std::size_t kKinds = 0xA94, kFirstKind = 0xA98;
inline constexpr std::size_t kA9C = 0xA9C, kAA0 = 0xAA0, kAA4 = 0xAA4;
inline constexpr std::size_t kRecordsBegin = 0xAA8, kRecordsEnd = 0xAAC, kRecordsCapacity = 0xAB0;
inline constexpr std::size_t kRecordList = 0xAB8, kStopwatch = 0xAC0, kAD8 = 0xAD8, kADC = 0xADC;
inline constexpr std::size_t kVehicleAE0 = 0xAE0;
// Part v2.
inline constexpr std::size_t kMaxCount = 0x3C, kAutomata = 0x44, kCount = 0x48;
inline constexpr std::size_t kTuningByte = 0xA3C, kScripts = 0xA80, kControllers = 0xA84, kA88 = 0xA88;
} // namespace field

// A node of the record list (+0xAB8).
namespace record {
inline constexpr std::size_t kRect1 = 0x14, kRect2 = 0x24, kObject = 0x34;
// Part v2: +8 and +0xC ints, +0x10 a word, the two rectangles, the object,
// +0x38 a byte (when set, the shutdown passes the object to the automata
// controller manager's slot 0x38 first), +0x3C a word. 0x40 bytes; the
// save keeps +8..+0x3F.
inline constexpr std::size_t kA = 0x08, kB = 0x0C, kC = 0x10, kHeld = 0x38, kD = 0x3C, kSize = 0x40;
} // namespace record

// Slots in bytes.
namespace slot {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08;
inline constexpr std::size_t kDeletingDestructor = 0x98; // 38
inline constexpr std::size_t kInExtendedBounds = 0xA4;  // 41
inline constexpr std::size_t kAutomatonClass = 0x110;   // 68
inline constexpr std::size_t kKindClass = 0x124, kKindGroup = 0x128, kKindRate = 0x12C; // 73, 74, 75
inline constexpr std::size_t kTestNeighbourCell = 0x148;                                 // 82
inline constexpr std::size_t kGetKindCurve = 0x154, kGetKindValue = 0x158;               // 85, 86
inline constexpr std::size_t kGetScale = 0x15C, kScaledMatrix = 0x160;                   // 87, 88
// Part v2.
inline constexpr std::size_t kInit = 0x0C, kShutdown = 0x10;                       // 3, 4
inline constexpr std::size_t kAddAutomaton = 0x1C, kRemoveAutomaton = 0x20;        // 7, 8
inline constexpr std::size_t kSetMaxCount = 0x5C, kHandleTuning = 0x60;            // 23, 24
inline constexpr std::size_t kSetCameraFollow = 0x88, kSlot49 = 0xC4;              // 34, 49
inline constexpr std::size_t kTuningSetUp = 0xE8, kForgetAutomaton = 0x114;        // 58, 69
// The serializable part's (+8) Write and Read, the message target's (+0xC)
// DoMessage and the +4 part's slot 3.
inline constexpr std::size_t kWrite = 0x0C, kRead = 0x10, kDoMessage = 0x0C, kDoMessage4 = 0x0C;
} // namespace slot

inline constexpr std::uint32_t kVehicleClassId = 0xA98F9D3B;
inline constexpr std::uint32_t kPedestrianClassId = 0xC98F9D17;
inline constexpr std::uint32_t kVehicleAutomatonClass = 0x896E714A;
inline constexpr std::uint32_t kPedestrianAutomatonClass = 0x896E75AF;
inline constexpr std::uint32_t kVehicleSize = 0xAE8, kPedestrianSize = 0xAE0;

// The city's slots for the two managers (cISC4City, slots 43 and 44).
inline constexpr std::size_t kCitySlotVehicles = 0xAC, kCitySlotPedestrians = 0xB0;

} // namespace opensc4::automata_vehmgr
