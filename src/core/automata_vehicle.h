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
//
// Part vb2 adds (the save, the setters, the life cycle):
//   the core's +0x3C a byte (Init does no more when set), +0xB0 an object
//          (referenced; slot 149 sets it from slot 148, saved as a reference
//          read back as 0xA9B40F05), +0xC0..+0xC4 a vector of 12-byte model
//          keys
//   the base's +0x1EC occupant is saved as a reference read back as
//          0xA821EF94; the route deque holds 3-byte cells in blocks of 0x7E
//          bytes
//   cSC4Vehicle's +0x260 a float (saved), +0x2A4..+0x2B0 four model objects
//          (held through a virtual base; Shutdown takes them off the view)
// Write and Read are the serializable part's slots 3 and 4: this is the
// object + 8 there.
//
// Part vb3 adds (the movement on frames):
//   the core's +0x4C the destination (SetDestination sets it; vb1's "second
//          point"), +0x58 the heading (a unit vector), +0x98 a word, +0xA0 a
//          float (slot 170 sets 100 when slot 122 answers), +0xA4 a vector
//          (three floats; slots 134 and 170 normalise it), +0xB4..+0xB8 a
//          vector slot 157 tests for elements
//   the base's +0x1F4 the groups word (slot 132), +0x1F8 the path (a vector
//          of 12-byte points: begin, end), +0x1FC the index on it
// The city's traffic network map (city slot 40, cSC4TrafficNetworkMap) and
// the network parts (an occupant's slot 49) are called by their slots.
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
// Part vb2's.
inline constexpr std::size_t kByte3C = 0x3C, kObjectB0 = 0xB0, kModelKeys = 0xC0;
inline constexpr std::size_t kFloat260 = 0x260, kModels = 0x2A4;
inline constexpr std::size_t kModelCount = 4;
// Part vb3's.
inline constexpr std::size_t kDestination = 0x4C, kHeading = 0x58;
inline constexpr std::size_t k98 = 0x98, kFloatA0 = 0xA0, kVectorA4 = 0xA4, kVectorB4 = 0xB4;
inline constexpr std::size_t kGroups1F4 = 0x1F4, kPath = 0x1F8, kPathIndex = 0x1FC;
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
// Part vb2's.
inline constexpr std::size_t kRemove = 0x34;          // 13 (header 10): Remove(bool), ret 4
inline constexpr std::size_t kSetFlag = 0x78;         // 30: SetAutomataFlag(flag, bool), ret 8
inline constexpr std::size_t kGetDefaultSpeed = 0x8C; // 35: the float +0x80 in ST0
inline constexpr std::size_t kSetSpeed = 0x90;        // 36: (float target, float rate), ret 8
inline constexpr std::size_t kSlot105 = 0x1A4;        // the core's 0x005096B0, no arguments
inline constexpr std::size_t kForget = 0x1F0;         // 124: cSC4Vehicle 0x0055BE70
inline constexpr std::size_t kSlot125 = 0x1F4;        // 125: a float in ST0, no arguments
inline constexpr std::size_t kSlot132 = 0x210;        // 132: four words, ret 0x10
inline constexpr std::size_t kSlot134 = 0x218;        // 134: 0x00550B60, no arguments
inline constexpr std::size_t kSlot148 = 0x250;        // 148: 0x00551C20, an object
inline constexpr std::size_t kGetByte1E8 = 0x264;     // 153: 0x00504FB0
inline constexpr std::size_t kSlot158 = 0x278;        // 158: 0x0053CC00 / 0x0055B690
inline constexpr std::size_t kSlot173 = 0x2B4;        // 173: 0x0054D860, two words, ret 8
inline constexpr std::size_t kSlot176 = 0x2C0;        // 176: cSC4Vehicle 0x0055DBE0, two words, ret 8
// The occupant part's (+4) slot 5: whether the object is initialised.
inline constexpr std::size_t kIsInitialised = 0x14;
// Part vb3's.
inline constexpr std::size_t kSetDestination = 0x28;  // 10 (header 7): 0x0054E5B0, (point), ret 4
inline constexpr std::size_t kSlot16 = 0x40;          // 16: the core's 0x005080C0, (point, heading), ret 8
inline constexpr std::size_t kFade = 0x84;            // 33: the core's 0x0050B360, two floats, ret 8
inline constexpr std::size_t kSmoothedNow = 0x88;     // 34: the smoothed value (0x004FA960) in ST0
inline constexpr std::size_t kSlot56 = 0xE0;          // 56: the core's 0x005098E0, an object or null
inline constexpr std::size_t kSlot57 = 0xE4;          // 57: the core's 0x0050B710, (word), ret 4
inline constexpr std::size_t kFrontPoint = 0x108;     // 66: (float* out), ret 4
inline constexpr std::size_t kSlot79 = 0x13C;         // 79: the core's 0x00505820, no arguments
inline constexpr std::size_t kSlot88 = 0x160;         // 88: the core's 0x004FADB0, a bool
inline constexpr std::size_t kSlot90 = 0x168;         // 90: the core's 0x004FADD0, (word), ret 4
inline constexpr std::size_t kSlot102 = 0x198;        // 102: the core's 0x0050B540, two words, ret 8
inline constexpr std::size_t kSlot122 = 0x1E8;        // 122: the core's 0x00509600, (float*), a bool
inline constexpr std::size_t kAdvanceOnPath = 0x214;  // 133: 0x005504E0, six words, ret 0x18
inline constexpr std::size_t kSlot141 = 0x234;        // 141: 0x00648EF0, a bool
inline constexpr std::size_t kClearAhead = 0x25C;     // 151: 0x00551300, a float in ST0
inline constexpr std::size_t kOffsetSideways = 0x274; // 157: 0x005510D0, (float*), ret 4
inline constexpr std::size_t kGo = 0x280;             // 160: 0x0054DA50
inline constexpr std::size_t kSlot161 = 0x284;        // 161: 0x0054E1A0 (part vb4), no arguments
inline constexpr std::size_t kWeighSides = 0x2A8;     // 170: 0x0054E790, (network, float*), ret 8
} // namespace slot

// Part vb3's slots of other objects.
namespace city {
inline constexpr std::size_t kTrafficNetwork = 0xA0;  // 40: cSC4TrafficNetworkMap
inline constexpr std::size_t kWidth = 0x164;          // 89: the city's size in x (a float in ST0)
inline constexpr std::size_t kDepth = 0x168;          // 90: in z
inline constexpr std::size_t kCellOf = 0x17C;         // 95: (x, z, int* cell x, int* cell z), ret 0x10
} // namespace city
namespace map {
inline constexpr std::size_t kChoose = 0x18;          // 6: (const float* weights, word), ret 8
inline constexpr std::size_t kOccupantAt = 0x2C;      // 11: (cell x, cell z), ret 8
inline constexpr std::size_t kCellList = 0x38;        // 14: (cell x, cell z), ret 8: nodes (+0 occupant, +0x54 next)
inline constexpr std::size_t kHeight = 0x48;          // 18: (x, z), ret 8, a float in ST0
inline constexpr std::size_t kLaneWord = 0x4C;        // 19: five words, ret 0x14: a word's address
inline constexpr std::size_t kAddAutomaton = 0x78;    // 30: (automaton, occupant), ret 8
inline constexpr std::size_t kRemoveAutomaton = 0x7C; // 31: (automaton, occupant, groups), ret 0xC
inline constexpr std::size_t kCanGo = 0xB0;           // 44: (from, to, kind), ret 0xC, a bool
} // namespace map
namespace manager {
inline constexpr std::size_t kStateWeight = 0xA8;     // 42: (state), ret 4, a float in ST0
inline constexpr std::size_t kKind = 0x110;           // 68: (automaton), ret 4
inline constexpr std::size_t kWeights = 0x120;        // 72: five words, ret 0x14
} // namespace manager
namespace part {
inline constexpr std::size_t kPath = 0x0C;            // 3: (key, word), ret 8: a path or null
inline constexpr std::size_t kFindPath = 0x14;        // 5: six words, ret 0x18
inline constexpr std::size_t kFindFrom = 0x24;        // 9: six words, ret 0x18
inline constexpr std::size_t kFlat = 0x40;            // 16: a bool, no arguments
inline constexpr std::size_t kIndexOf = 0x4C;         // 19: (point, path), ret 8: an index
inline constexpr std::size_t kEndPoint = 0x58;        // 22: (path, float* out), ret 8
} // namespace part
inline constexpr std::size_t kOccupantHeight = 0xE0;  // an occupant's slot 56: (x, z), ret 8, a float in ST0
inline constexpr std::size_t kOccupantFlag = 0x20;    // an occupant's slot 8: (flag), ret 4, a bool
inline constexpr std::uint32_t kTunnelIid = 0x8A4BD52B; // the interface slot 132 asks the occupants for

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kUint8 = 0x14, kUint16 = 0x1C, kSint32 = 0x20, kUint32 = 0x24, kFloat32 = 0x30;
} // namespace stream

inline constexpr std::uint32_t kObjectB0Iid = 0xA9B40F05; // the iid +0xB0 is read back as
inline constexpr std::uint32_t kOccupantIid = 0xA821EF94; // the iid +0x1EC is read back as
inline constexpr std::uint32_t kSaveVersion = 2;          // both classes' Write and Read

// The slots of other objects this part calls.
inline constexpr std::size_t kOccupantGroupSlot = 0x58;  // the occupant (+0x1EC): (2) bool
inline constexpr std::size_t kNetworkPathSlot = 0x90;    // the network part (+0x1F0): five words, bool
inline constexpr std::size_t kNetworkPartSlot = 0xC4;    // an occupant's network part
inline constexpr std::size_t kNeighbourSlot = 0xD4;      // the network argument of slot 171: three words, bool
inline constexpr std::size_t kCityVehicleManager = 0xAC; // the city's slot 43
inline constexpr std::size_t kServiceNeighbour = 0x3C;   // 0x00B43D54's: (occupant, side, 1, kind), ret 0x10
inline constexpr std::size_t kServiceList = 0x40;        // 0x00B43D54's: (key), ret 4: an object, its list at +0x1C
inline constexpr std::size_t kManagerAutomata = 0x44;    // the automata managers' list (0xC-byte nodes, +8)
inline constexpr std::size_t kManagerForget = 0x114;     // the vehicle manager's slot 69 (automaton), ret 4
inline constexpr std::size_t kQueueKeySlot = 0xEC;       // a +0x27C element's object: (int* out, int* out), ret 8
inline constexpr std::size_t kViewRemoveSlot = 0x74;     // the view 0x00B43DD0's (model), ret 4
inline constexpr std::size_t kTransformSlot = 0x24;      // 0x00B43DDC's: a bool, no arguments
inline constexpr std::size_t kStringToChar = 0x14;       // cIGZString's ToChar

} // namespace opensc4::automata_vehicle
