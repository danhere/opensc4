// The layout of the automaton base and cSC4Vehicle, part vb1 (build 641;
// functions, vtables and data in data/addresses/automata.toml, the survey in
// docs/automata.md, "Part vb").
//
// Three levels:
//   - the automata core (constructor 0x005140A0, vtable 0x00A95AC0;
//     "cSC4AutomatonCore", the name is ours), which every automaton class
//     derives from, the aircraft and boats directly;
//   - the automaton base (constructor 0x005544D0, vtable 0x00A9C068;
//     "cSC4AutomatonBase", the name is ours), which the pedestrians and the
//     road vehicles derive from;
//   - cSC4Vehicle (class ID 0x896E714A, factory 0x005148E0, constructor
//     0x005628F0, 0x2B8 bytes from a pool of its own), which the other road
//     vehicles, the trains and the emergency vehicles derive from.
//
// Six parts, each with its own vtable:
//   +0x00 the automaton (cISC4Automaton: slots 3 to 69 are the gzcom-dll
//         header's 0 to 66; then the class's own: 176 slots in the base,
//         192 in cSC4Vehicle)
//   +0x04 the occupant (cISC4Occupant, 34 slots)
//   +0x08 the serializable part (7 slots: Write 3, Read 4, GetGZCLSID 5)
//   +0x0C a 25-slot part (slot 22 the deleting destructor, 23 and 24 Write
//         and Read through +4)
//   +0x20 a 5-slot part (6 in cSC4Vehicle), +0x38 a 4-slot part
// Each slot 0 of the other parts is an adjustor thunk to slot 0.
//
// The core's fields this part reads or writes:
//   +0x28 the occupant type (0xC7CC75F1 for cSC4Vehicle)
//   +0x33 a byte of flags (the base sets bit 0x10)
//   +0x40 the position (three floats; slot 7 copies it out)
//   +0x4C a second point (slot 143 answers its address)
//   +0x7C the radius (slot 17), +0x80 a float cSC4Vehicle draws
//   +0x84 the automata flags (slot 31 tests them)
//   +0x1A0 a smoothed value (four words; 0x004FA960 reads it)
// The base's own (0x005544D0):
//   +0x1E4 a state (7), +0x1E8 a byte
//   +0x1EC an occupant (referenced), +0x1F0 a network part (referenced)
//   +0x1F4 four bytes (0xFFFF: +0x1F4 and +0x1F5 0xFF), +0x1F7's low nibble
//          a network kind
//   +0x1F8, +0x1FC, +0x200 a float
//   +0x204 a deque of 0xC-byte elements (a referenced object first; blocks
//          of 0x78 bytes)
//   +0x22C a route deque (SC4RouteDeque); the base ends at +0x254
// cSC4Vehicle's (0x005628F0):
//   +0x254..+0x260 zero; +0x258 and +0x25C objects released through a
//          virtual base (their +4 vbtable's second word)
//   +0x264 and +0x270 two points (the core's default point, 0x00B09A04)
//   +0x27C a deque of 8-byte elements (a referenced object first; blocks of
//          0x80 bytes)
//   +0x2A4..+0x2B0 zero, +0x2B4 a byte
//
// A deque (both kinds): +0x00 the start iterator (current, first, last,
// node), +0x10 the finish iterator, +0x20 the map, +0x24 the map's size.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_vehicle {

inline constexpr std::uint32_t kVehicleClassId = 0x896E714A;
inline constexpr std::uint32_t kBaseInterfaceId = 0x8B99EE7C; // the base's QueryInterface answers this
inline constexpr std::uint32_t kVehicleType = 0xC7CC75F1;     // the occupant type cSC4Vehicle sets
inline constexpr std::uint32_t kVehicleSize = 0x2B8;

namespace field {
inline constexpr std::size_t kVtable = 0x00, kVtable4 = 0x04, kSerializable = 0x08, kVtableC = 0x0C;
inline constexpr std::size_t kVtable20 = 0x20, kVtable38 = 0x38;
// The core's.
inline constexpr std::size_t kType = 0x28, kFlags33 = 0x33;
inline constexpr std::size_t kPosition = 0x40, kPoint4C = 0x4C;
inline constexpr std::size_t kRadius = 0x7C, kDraw = 0x80, kAutomataFlags = 0x84;
inline constexpr std::size_t kSmoothed = 0x1A0;
// The base's.
inline constexpr std::size_t kState = 0x1E4, kByte1E8 = 0x1E8;
inline constexpr std::size_t kOccupant = 0x1EC, kNetwork = 0x1F0;
inline constexpr std::size_t kBytes1F4 = 0x1F4, kByte1F5 = 0x1F5, kKind1F7 = 0x1F7;
inline constexpr std::size_t k1F8 = 0x1F8, k1FC = 0x1FC, kFloat200 = 0x200;
inline constexpr std::size_t kDeque204 = 0x204, kRoute = 0x22C, kBaseEnd = 0x254;
// cSC4Vehicle's.
inline constexpr std::size_t kVehicle254 = 0x254, kVirtual258 = 0x258, kVirtual25C = 0x25C;
inline constexpr std::size_t kPoint264 = 0x264, kPoint270 = 0x270, kDeque27C = 0x27C;
inline constexpr std::size_t kVehicle2A4 = 0x2A4, kByte2B4 = 0x2B4;
} // namespace field

namespace deque {
inline constexpr std::size_t kStart = 0x00, kFinish = 0x10, kMap = 0x20, kMapSize = 0x24, kSize = 0x28;
// An iterator: current, first, last, node.
inline constexpr std::size_t kCurrent = 0x0, kFirst = 0x4, kLast = 0x8, kNode = 0xC;
} // namespace deque

namespace slot {
// cIGZUnknown.
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08;
// cISC4Automaton (header slot + 3).
inline constexpr std::size_t kGetPosition = 0x1C, kGetRadius = 0x44, kCheckFlag = 0x7C;
// The class's own.
inline constexpr std::size_t kCanMeet = 0x124;        // 73: (other automaton) bool (the core's 0x005059C0)
inline constexpr std::size_t kIsGone = 0x154;         // 85: the core's flag 1 test
inline constexpr std::size_t kGroups = 0x220;         // 136: 0x00504F70, +0x1F4
inline constexpr std::size_t kPosition = 0x230;       // 140: the position's address
inline constexpr std::size_t kPoint = 0x23C;          // 143: the second point's address
inline constexpr std::size_t kNear = 0x248;           // 146: 0x0054DAB0
inline constexpr std::size_t kFlag8 = 0x258;          // 150: 0x00504FA0
inline constexpr std::size_t kNetworkTest = 0x260;    // 152: 0x0054EB90
inline constexpr std::size_t kDirectionTo = 0x26C;    // 155: 0x0054EA80
inline constexpr std::size_t kBestDirection = 0x2AC;  // 171: 0x0054D8D0
inline constexpr std::size_t kManager = 0x2B8;        // 174: cSC4Vehicle 0x0055BF60
inline constexpr std::size_t kNearestVehicle = 0x1EC; // 123: cSC4Vehicle 0x005573C0
} // namespace slot

// The slots of other objects this part calls.
inline constexpr std::size_t kOccupantGroupSlot = 0x58;  // the occupant (+0x1EC): (2) bool
inline constexpr std::size_t kNetworkPathSlot = 0x90;    // the network part (+0x1F0): five words, bool
inline constexpr std::size_t kNetworkPartSlot = 0xC4;    // an occupant's network part
inline constexpr std::size_t kNeighbourSlot = 0xD4;      // the network argument of slot 171: three words, bool
inline constexpr std::size_t kCityVehicleManager = 0xAC; // the city's slot 43
inline constexpr std::size_t kServiceNeighbour = 0x3C;   // 0x00B43D54's: (occupant, side, 1, kind), ret 0x10
inline constexpr std::size_t kServiceList = 0x40;        // 0x00B43D54's: (key), ret 4: an object, its list at +0x1C
inline constexpr std::size_t kManagerAutomata = 0x44;    // the automata managers' list (0xC-byte nodes, +8)

} // namespace opensc4::automata_vehicle
