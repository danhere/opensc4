// The layout of the automata script system, part s1 (build 641; class ID,
// vtables and functions in data/addresses/automata.toml, the survey in
// docs/automata.md, "Part s"): cSC4AutomataScriptSystem (0x4A1B664E,
// factory 0x005148A0, constructor 0x005207A0, 0xB8 bytes, city slot 48,
// GetAutomataScriptSystem at the city vtable's byte offset 0xC0) and the occupant group
// template it makes (cSC4OccupantGroupTemplate, constructor 0x005206B0,
// 0x84 bytes).
//
// The script system has four parts, each with its own vtable: +0x00 the
// cISC4AutomataScriptSystem interface (19 slots; each of the gzcom-dll
// header's overloaded pairs sits in the vtable in the other order), +0x04
// (QueryInterface, AddRef, Release and a shared stub), +0x08 (the same and
// 0x0051C4B0, an ID test) and +0x0C the serializable part (its Write and
// Read are shared stubs; GetGZCLSID 0x00520050).
//
// Fields (constructor 0x005207A0):
//   +0x10 initialized (Init, Shutdown), +0x14 the reference count
//   +0x18 an object Shutdown shuts down (slot 4) and releases
//   +0x1C a 0x2C-byte record that starts with an SC4String (0x005C0EB0,
//         0x005C0F40)
//   +0x48 a byte (Init clears it before the scripts load)
//   +0x4C hash map name -> occupant group (SC4String keys; the bucket
//         vector at +0x50..+0x58, the count at +0x5C)
//   +0x60 hash map name -> automata group (the groups made with the
//         automata flag; buckets +0x64.., count +0x70)
//   +0x74 hash map ID -> occupant group (uint32 keys; buckets +0x78..,
//         count +0x84)
//   +0x88 the list of groups without an ID (the special filter groups;
//         0xC-byte nodes, the group at +8)
//   +0x8C hash map name -> controller template (buckets +0x90..,
//         count +0x9C)
//   +0xA0 the list of track building groups
//   +0xA4 hash map key -> drive command (uint32 keys; buckets +0xA8..,
//         count +0xB4; 0xD when not found)
//
// The string hash maps' nodes: +0 next, +4 the key's begin, +8 its end,
// +0xC its capacity, +0x10 the value; the hash is h * 5 + the signed char
// over the key, modulo the bucket count. The uint32 maps' nodes: +0 next,
// +4 the key, +8 the value; the bucket is the key modulo the count.
//
// The occupant group template (constructor 0x005206B0) derives from the
// occupant filter base (0x005EA080, destructor 0x005E95C0). Its fields:
//   +0x00 vtable 0x00A96C0C (slot 1 AddRef, 2 Release, 3 Matches
//         0x0051C3D0, 6 the deleting destructor 0x00520780), +0x04 the
//         base's second vtable, 0x00A96BF4
//   +0x0C the parent group, +0x10 the group ID, +0x14 an automaton class
//   +0x18 an SC4ScriptNumber (0x009166D4, 0x00915EB5)
//   +0x2C the percentage (float, 1.0)
//   +0x30 a list (0xC-byte nodes), +0x34 a name hash map (buckets
//         +0x38.., count +0x44), +0x48 a list (0x14-byte nodes)
//   +0x4C, +0x4E two words, -2
//   +0x50 a list (0xC-byte nodes), +0x54 a list (0x1C-byte nodes)
//   +0x60, +0x64 two objects from operator_new, +0x68, +0x6C zero
//   +0x70 a hash map of 0x3C-byte nodes with three SC4Strings (buckets
//         +0x74.., count +0x80)
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_scripts {

inline constexpr std::uint32_t kClassId = 0x4A1B664Eu;
inline constexpr std::uint32_t kScriptSystemIid = 0x8A1B6647u;
inline constexpr std::size_t kObjectSize = 0xB8;
inline constexpr std::size_t kGroupSize = 0x84;

namespace field {
inline constexpr std::size_t kVtable = 0x00, kVtable4 = 0x04, kVtable8 = 0x08, kSerializable = 0x0C;
inline constexpr std::size_t kInitialized = 0x10, kRefCount = 0x14, kObject18 = 0x18, kSource = 0x1C;
inline constexpr std::size_t kLoadByte = 0x48;
inline constexpr std::size_t kGroupsByName = 0x4C, kAutomataGroups = 0x60, kGroupsById = 0x74;
inline constexpr std::size_t kSpecialGroups = 0x88, kTemplates = 0x8C, kTrackGroups = 0xA0, kDriveCommands = 0xA4;
} // namespace field

// A hash map's parts, from its field: +4 the bucket vector's begin, +8 its
// end, +0xC its capacity, +0x10 the count.
namespace map {
inline constexpr std::size_t kBegin = 0x04, kEnd = 0x08, kCount = 0x10;
} // namespace map

namespace group {
inline constexpr std::size_t kVtable = 0x00, kVtable4 = 0x04;
inline constexpr std::size_t kParent = 0x0C, kId = 0x10, kClass = 0x14, kNumber = 0x18, kPercentage = 0x2C;
inline constexpr std::size_t kList30 = 0x30, kNames = 0x34, kList48 = 0x48, kWord4C = 0x4C, kWord4E = 0x4E;
inline constexpr std::size_t kList50 = 0x50, kList54 = 0x54, kObject60 = 0x60, kObject64 = 0x64, kZero68 = 0x68;
inline constexpr std::size_t kZero6C = 0x6C, kRecords = 0x70;
} // namespace group

namespace slot {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kShutdown = 0x10;
inline constexpr std::size_t kGetOccupantGroupById = 0x2C, kCreateOccupantGroup = 0x34;
// The occupant group template's.
inline constexpr std::size_t kGroupMatches = 0x0C, kGroupDeletingDestructor = 0x18;
} // namespace slot

} // namespace opensc4::automata_scripts
