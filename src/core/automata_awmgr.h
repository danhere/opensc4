// The layout of the aircraft and watercraft managers and of the base all
// four automata managers share, part aw1 (build 641; class IDs, vtables and
// functions in data/addresses/automata.toml, the survey in
// docs/automata.md, "Part aw"):
//   cSC4AircraftManager (0xC98F9D4A, factory 0x00514600, constructor
//     0x004FBB40, 0xA9C bytes, city slot 45),
//   cSC4WatercraftManager (0xA98F9D28, factory 0x00514630, constructor
//     0x00574860, 0xAD4 bytes, city slot 46),
//   both on the common base (constructor 0x005125E0, vtable 0x00A95880,
//     GetGZCLSID 0x498F9CB5; "cSC4AutomataManager" here), which the
//     vehicle and pedestrian managers' base (part v1) derives from too.
//
// Four parts, each with its own vtable: +0x00 the manager (cISC4Automata
// Manager, 68 slots: the gzcom-dll header's slots from Init at 3; each
// overloaded pair sits in the other order), +0x04 a four-slot interface
// (the common base's slot 3, 0x0050DD30), +0x08 the serializable part
// (Write, Read, GetGZCLSID; the factories answer this part) and +0x0C a
// message target. The aircraft and watercraft managers have no destructor
// body of their own vtables: their deleting destructors free their fields
// and go to the common base's 0x00512780.
//
// The common base's fields (its constructor):
//   +0x10 a byte, +0x14..+0x20 and +0x24..+0x30 two float rectangles
//   +0x34 initialized, +0x38 the reference count
//   +0x3C the most automata (SetMaxCount), +0x40 a share of it
//   +0x44 the automata list (0xC-byte nodes, the automaton at +8), +0x48
//         its count
//   +0x4C active (slot 5 sets it, slot 6 answers it; 1 from the
//         constructor), +0x4D paused (slots 9, 10, 11)
//   +0x50 the manager's generator (0x9C8 bytes, SC4Random::ConstructSeeded
//         with seed -1, a clock seed)
//   +0xA18/+0xA1C/+0xA20 the exemplar vector (0x6C-byte tExemplarData,
//         slot 28; slot 44 finds one by its resource key at +0x10)
//   +0xA24 the exemplar map's header (std::map, 0x20-byte nodes, the key at
//         +0x10; slot 29), +0xA28 its size; slot 30 tests a group ID there
//   +0xA30/+0xA34/+0xA38 zero, +0xA3C the least zoom (slot 14), +0xA3D a
//         byte (part v1's scale switch)
//   +0xA40 a word (0x18 aircraft, 0x19 watercraft), +0xA44 the occupant
//         group (0x4003, 0x4412), +0xA48 the automaton class (cSC4Aircraft
//         0xC96E75DC, cSC4Watercraft 0x096E75F5), +0xA4C a byte
//   +0xA50 a hash table of hash sets (the group instances, slot 31's), +0xA64
//         a hash table of counts by ID (the source buildings, slot 36), both
//         0x14 bytes: +4/+8 the buckets, +0x10 the count
//   +0xA78 a byte, +0xA7C the cells object, +0xA80 the script system, +0xA84
//         the automata controller manager (set by Init)
// The aircraft manager's: +0xA88 a std::map's header (0x18-byte nodes: an
// object at +0x10 and another at +0x14, both referenced), +0xA8C its size,
// +0xA94 a list head (0x18-byte nodes), +0xA98 an occupant filter (Init).
// The watercraft manager's: +0xA88 a byte, three hash tables at +0xA8C (0x20-
// byte nodes: references at +4, +8, +0xC and +0x1C, a vector of 0x3C-byte
// records at +0x10/+0x14), +0xAA0 and +0xAB4 (0xC-byte nodes: references at
// +4 and +8), and a vector of references at +0xAC8.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_awmgr {

namespace field {
inline constexpr std::size_t kVtable = 0x00, kVtable4 = 0x04, kSerializable = 0x08, kMessage = 0x0C;
inline constexpr std::size_t kByte10 = 0x10, kRect = 0x14, kRect2 = 0x24;
inline constexpr std::size_t kInitialized = 0x34, kRefCount = 0x38, kMaxCount = 0x3C, kShare = 0x40;
inline constexpr std::size_t kAutomata = 0x44, kCount = 0x48, kActive = 0x4C, kPaused = 0x4D, kRandom = 0x50;
inline constexpr std::size_t kExemplarsBegin = 0xA18, kExemplarsEnd = 0xA1C, kExemplarsCapacity = 0xA20;
inline constexpr std::size_t kExemplarMap = 0xA24, kExemplarMapSize = 0xA28;
inline constexpr std::size_t kA30 = 0xA30, kA34 = 0xA34, kA38 = 0xA38, kMinZoom = 0xA3C, kA3D = 0xA3D;
inline constexpr std::size_t kA40 = 0xA40, kGroup = 0xA44, kAutomatonClass = 0xA48, kA4C = 0xA4C;
inline constexpr std::size_t kGroupInstances = 0xA50, kSourceCounts = 0xA64, kA78 = 0xA78;
inline constexpr std::size_t kCells = 0xA7C, kScripts = 0xA80, kControllers = 0xA84;
// The aircraft manager's.
inline constexpr std::size_t kAirMap = 0xA88, kAirMapSize = 0xA8C, kAirList = 0xA94, kAirFilter = 0xA98;
// The watercraft manager's.
inline constexpr std::size_t kWaterByte = 0xA88, kWaterTable = 0xA8C, kWaterTable2 = 0xAA0, kWaterTable3 = 0xAB4;
inline constexpr std::size_t kWaterRefs = 0xAC8; // begin, end, capacity
} // namespace field

// A hash table (SC4Uint32HashSet::Construct's object, 0x14 bytes).
namespace table {
inline constexpr std::size_t kBegin = 0x04, kEnd = 0x08, kCapacity = 0x0C, kCount = 0x10, kSize = 0x14;
inline constexpr std::uint32_t kBuckets = 0x64; // the constructors' bucket hint
} // namespace table

// A std::map node (the header has the same shape): +0 the colour byte, +4
// the parent (the root for the header), +8 the left (the leftmost), +0xC the
// right (the rightmost); a null child is null. The key at +0x10.
namespace tree {
inline constexpr std::size_t kParent = 0x04, kLeft = 0x08, kRight = 0x0C, kKey = 0x10, kValue = 0x14;
} // namespace tree

// A tExemplarData record of the +0xA18 vector: the resource key (type,
// group, instance) at +0x10, a vtable 0x00A80810 base at +0x1C with a
// buffer at +0x20.
namespace exemplar {
inline constexpr std::size_t kKey = 0x10, kSize = 0x6C;
} // namespace exemplar

// Slots in bytes (cISC4AutomataManager; the slot numbers count from
// QueryInterface at 0).
namespace slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08;
inline constexpr std::size_t kShutdown = 0x10;                              // 4
inline constexpr std::size_t kIsActive = 0x18;                              // 6
inline constexpr std::size_t kPause = 0x24, kResume = 0x28, kIsPaused = 0x2C; // 9, 10, 11
inline constexpr std::size_t kIsDrivingMode = 0x34, kGetMinZoom = 0x38;     // 13, 14
inline constexpr std::size_t kDispatch5 = 0x44;                             // 17
inline constexpr std::size_t kGetExemplarData = 0x70, kGetExemplarDataMap = 0x74, kCanCreateGroup = 0x78; // 28-30
inline constexpr std::size_t kIsCameraFollowUpdating = 0x8C;                // 35
inline constexpr std::size_t kGetSourceBuildingCount = 0x90;                // 36
inline constexpr std::size_t kGetSourceBuildingCountForGroup = 0x94;        // 37
inline constexpr std::size_t kDeletingDestructor = 0x98;                    // 38
inline constexpr std::size_t kFindExemplar = 0xB0;                          // 44
// The serializable part's GetGZCLSID (+8, slot 5).
inline constexpr std::size_t kGetGZCLSID = 0x14;
} // namespace slot

inline constexpr std::uint32_t kAircraftClassId = 0xC98F9D4A;
inline constexpr std::uint32_t kWatercraftClassId = 0xA98F9D28;
inline constexpr std::uint32_t kBaseClassId = 0x498F9CB5;   // the common base's GetGZCLSID
inline constexpr std::uint32_t kManagerIid = 0x098F9BF4;    // cISC4AutomataManager
inline constexpr std::uint32_t kSerializableIid = 0xE4FDA3D4, kMessageIid = 0x452294AA;
inline constexpr std::uint32_t kAircraftAutomatonClass = 0xC96E75DC, kWatercraftAutomatonClass = 0x096E75F5;
inline constexpr std::uint32_t kAircraftSize = 0xA9C, kWatercraftSize = 0xAD4;

// The city's slots for the two managers (cISC4City, slots 45 and 46).
inline constexpr std::size_t kCitySlotAircraft = 0xB4, kCitySlotWatercraft = 0xB8;

} // namespace opensc4::automata_awmgr
