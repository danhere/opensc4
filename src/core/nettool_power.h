// The power line tool's layout (build 641; part w1 of docs/nettool.md),
// shared by src/core/nettool_power.cpp (its life, the slots that keep to its
// own lists and the template code of its vectors) and
// src/core/nettool_power_2.cpp (the pole records, the occupant lookups,
// slots 53 and 10). Both halves are one group, simulators.nettool.power:
// nettool_power.cpp registers it and its install also installs the second
// half's list (InstallPower2).
//
// The tool (0x3B8 bytes, constructor 0x006503C0) is the general tool (0x368
// bytes) and:
//   +0x368 uint8 Init ran (Shutdown clears it)
//   +0x36C the pole records: a vector of 0x30-byte records (below)
//   +0x378 a vector of 0xC-byte vectors of 16-byte elements (template code
//         0x0064F770 assigns one; freed by SC4ShortVectorVector::Destroy)
//   +0x384 uint32 the count GetCostOfSolution (slot 20) multiplies by the
//         float +0x3B0; +0x388 uint32 the pole count it multiplies by +0x3AC
//   +0x38C, +0x390 two lists (0xC-byte nodes: next, previous, an occupant)
//         slot 16 empties; +0x394 a list of 0x14-byte nodes (next, previous,
//         two poles) slot 16 unwires and empties
//   +0x398 a vector of 0xC-byte vectors of 32-bit values (SC4IntVector)
//   +0x3A4 uint32 (exemplar property 0x098B25C8, 0x14 by Init),
//         +0x3A8 float (0x098B27D0, 7.0 by Init), +0x3AC uint32 the cost of a
//         pole (0x4A5F5953), +0x3B0 float the cost of a cell of wire
//         (0x0A5F596B): read by Init from the exemplar 0x6534284A,
//         0xE7E2C2DB, 0xC911E35B; the constructor leaves +0x3A4 and +0x3A8
//         unwritten.
//
// A pole record (0x30 bytes): +0x0 its index in the vector, +0x4 x, +0x8 z,
// +0xC the dragged cell's index, +0x10 the pole occupant, +0x14 uint8 the
// pole is new (made by 0x00650140), +0x15 uint8 the wire sides (one bit per
// direction 0x0061F4C0 gives), +0x18 a vector of 8-byte pairs (the records
// wired to: index, a word), +0x24 and +0x28 two referenced objects
// (released by 0x006505D0), +0x2C and +0x2D two bytes (0x0064F170 compares
// +0x24 to +0x2D with a key). Copying a record copies the words +0x0 to
// +0x10, the two bytes, the vector (copy-constructed 0x007CED60 or assigned
// 0x00611B00) and the words +0x24 to +0x2C; the two bytes +0x16 and +0x17
// are never copied.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {

namespace power {
inline constexpr std::uintptr_t kVtable4 = 0x00AA9F20;
inline constexpr std::size_t kInited = 0x368, kPoles = 0x36C, kPolesEnd = 0x370, kPolesCap = 0x374;
inline constexpr std::size_t kRuns = 0x378, kRunsEnd = 0x37C, kCount384 = 0x384, kCount388 = 0x388;
inline constexpr std::size_t kList38C = 0x38C, kList390 = 0x390, kWires = 0x394, kIntVectors = 0x398;
inline constexpr std::size_t kTuning3A4 = 0x3A4, kTuning3A8 = 0x3A8, kPoleCost = 0x3AC, kWireCost = 0x3B0;
inline constexpr uint32_t kType = 5;
inline constexpr uint32_t kListHeadSize = 0xC, kWireHeadSize = 0x14;
// Init's exemplar and properties.
inline constexpr uint32_t kTuningType = 0x6534284A, kTuningGroup = 0xE7E2C2DB, kTuningInstance = 0xC911E35B;
inline constexpr uint32_t kResourceIid = 0xA52160F5;
inline constexpr uint32_t kProp3A4 = 0x098B25C8, kProp3A8 = 0x098B27D0, kPropPoleCost = 0x4A5F5953;
inline constexpr uint32_t kPropWireCost = 0x0A5F596B;
inline constexpr uint32_t kInit3A4 = 0x14, kInit3A8Bits = 0x40E00000; // 20 and 7.0f
// The pole exemplars by wire sides (0x00B467B0, a word per sides byte;
// filled by the ini section "PowerPoles" of the network ini, 0x8A5971C5/
// 0x8A5993B9, once per run: the flag 0x00B467A8).
inline constexpr std::uintptr_t kPoleExemplars = 0x00B467B0, kLoadedOnce = 0x00B467A8;
inline constexpr std::uintptr_t kSectionName = 0x00AA9F14; // "PowerPoles"
inline constexpr uint32_t kIniGroup = 0x8A5971C5, kIniInstance = 0x8A5993B9;
} // namespace power

namespace pole {
inline constexpr std::size_t kSize = 0x30;
inline constexpr std::size_t kIndex = 0x0, kX = 0x4, kZ = 0x8, kCell = 0xC, kOccupant = 0x10, kNew = 0x14;
inline constexpr std::size_t kSides = 0x15, kLinks = 0x18, kLinksEnd = 0x1C, kLinksCap = 0x20;
inline constexpr std::size_t kObject24 = 0x24, kObject28 = 0x28, kWord2C = 0x2C;
} // namespace pole

// The second half's detours (nettool_power_2.cpp); called by the group's
// install in nettool_power.cpp.
bool InstallPower2();

} // namespace opensc4::nettool_sim
