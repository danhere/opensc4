// The layout of the emergency vehicles, parts em1, em2a, em2b and em3a (build 641; functions, vtables and data in
// data/addresses/automata.toml, the survey in docs/automata.md, "Part em").
//
// A base on cSC4Vehicle (src/core/automata_vehicle.h) and three classes on
// it, each with its own six vtables (+0x00, +0x04, +0x08, +0x0C, +0x20, +0x38
// as cSC4Vehicle's):
//   - the base (no class ID or factory of its own, constructor 0x00532190,
//     main vtable 0x00A981E0, 199 slots: adds 192 to 198)
//   - cSC4FireTruck (0xAA222B83, factory 0x00514660, constructor 0x005337E0,
//     0x2E4 bytes, 200 slots: adds 199)
//   - cSC4PatrolCar (0x4A222B98, 0x00514680, 0x00544240, 0x2DC bytes, 201
//     slots: adds 199 and 200)
//   - cSC4Ambulance (0x6C1ACDE1, 0x00514740, 0x00504F30, 0x2D8 bytes, 199
//     slots)
// All from SC4::operator_new, back through SC4::operator_delete.
//
// The base's fields:
//   +0x2B8 a point (three copies of the float 0x00B22814 at construction;
//          slot 143 answers its address)
//   +0x2C4 an object (referenced; read back as 0x26BB767E)
//   +0x2C8 a byte, +0x2CA a word (slots 23 and 24)
//   +0x2CC the mission (slot 26; slot 25 sets it)
//   all saved
//   +0x2D0 an object held through its virtual base (slot 60 sets the siren
//          by whether it is null), +0x2D4 the siren's sound (slot 194);
//          neither saved
// The fire truck's:
//   +0x2D8 and +0x2DC two occupant filters (made by the constructor; not
//          saved), +0x2E0 an object (referenced, read back as 0xA9B40F05;
//          the city's slot 44 object is told of it at Shutdown)
// The patrol car's:
//   +0x2D8 an object as the fire truck's +0x2E0
// The ambulance adds none.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_emergency {

inline constexpr std::uint32_t kFireTruckClassId = 0xAA222B83;
inline constexpr std::uint32_t kPatrolCarClassId = 0x4A222B98;
inline constexpr std::uint32_t kAmbulanceClassId = 0x6C1ACDE1;

inline constexpr std::size_t kFireTruckSize = 0x2E4, kPatrolCarSize = 0x2DC, kAmbulanceSize = 0x2D8;

namespace base {
inline constexpr std::size_t kPoint = 0x2B8, kTarget = 0x2C4, kByte = 0x2C8, kWord = 0x2CA, kMission = 0x2CC;
inline constexpr std::size_t kHeld = 0x2D0, kSiren = 0x2D4;
} // namespace base

namespace fire {
inline constexpr std::size_t kFilter = 0x2D8, kFilter2 = 0x2DC, kCrew = 0x2E0;
} // namespace fire

namespace patrol {
inline constexpr std::size_t kCrew = 0x2D8;
} // namespace patrol

inline constexpr std::uint32_t kTargetIid = 0x26BB767E; // the base's +0x2C4 is read back as this
inline constexpr std::uint32_t kCrewIid = 0xA9B40F05;   // the fire truck's +0x2E0 and the patrol car's +0x2D8

// The slots of other objects this part calls, of the automaton unless noted.
namespace slot {
inline constexpr std::size_t kInitialized = 0x14;   // the occupant part's slot 5
inline constexpr std::size_t kRemove = 0x34;        // 13: (bool), ret 4
inline constexpr std::size_t kSetMission = 0x64;    // the base's 25: (mission), ret 4
inline constexpr std::size_t kMission = 0x68;       // the base's 26
inline constexpr std::size_t kCheckFlag = 0x7C;     // 31: (flag), a bool
inline constexpr std::size_t kSetSpeed = 0x90;      // 36: (target, rate), ret 8
inline constexpr std::size_t kPushState = 0xD0;     // 52: seven words, ret 0x1C
inline constexpr std::size_t kInState = 0xE4;       // 57: (type), ret 4: the stacked state of the type, or null
inline constexpr std::size_t kStateType = 0xEC;     // 59: the current state's type
inline constexpr std::size_t kSlot79 = 0x13C;       // 79
inline constexpr std::size_t kSetSiren = 0x308;     // the base's 194: (bool), ret 4
inline constexpr std::size_t kCityCrewService = 0xB0; // the city's 44
inline constexpr std::size_t kCrewDone = 0x20;      // that object's 8: (object, 0), ret 8
inline constexpr std::size_t kSoundStop = 0x2C;     // 0x00B43CB8's 11: five words, ret 0x14
// Part em2a's, of the automaton unless noted.
inline constexpr std::size_t kAsOccupant = 0x14;    // 5: the occupant part (this + 4)
inline constexpr std::size_t kSetFlag = 0x78;       // 30: (flag, bool), ret 8
inline constexpr std::size_t kFade = 0x84;          // 33: (float, float), ret 8
inline constexpr std::size_t kSetState = 0xCC;      // 51: seven words, ret 0x1C
inline constexpr std::size_t kCurrentState = 0xE0;  // 56: the current state
inline constexpr std::size_t kSlot99 = 0x18C;       // 99: (2, &held), ret 8
inline constexpr std::size_t kSlot102 = 0x198;      // 102: (a, b), ret 8
inline constexpr std::size_t kPoint = 0x23C;        // 143: the point's address
inline constexpr std::size_t kAtPoint = 0x248;      // the base's 146
inline constexpr std::size_t kSlot195 = 0x30C;      // the base's 195 to 198: (seconds), ret 4
inline constexpr std::size_t kSlot196 = 0x310;
inline constexpr std::size_t kSlot197 = 0x314;
inline constexpr std::size_t kSlot198 = 0x318;
inline constexpr std::size_t kInGroup = 0x58;       // the occupant part's 22: (group), a bool
inline constexpr std::size_t kCellOf = 0x17C;       // the city's 95: (x, z, int*, int*), ret 0x10
inline constexpr std::size_t kCityEffects = 0xBC;   // the city's 47: the sounds' service
inline constexpr std::size_t kEffectsMake = 0x14;   // that service's 5: (occupant, SC4String*, 0), ret 0xC: a sound
inline constexpr std::size_t kEffectsRemove = 0x1C; // its 7: (sound), ret 4
inline constexpr std::size_t kSirenStop = 0x48;     // the siren sound's 18: (0), ret 4
inline constexpr std::size_t kSirenSlot4 = 0x10;    // the siren sound's 4
inline constexpr std::size_t kOccupantsAt = 0x38;   // 0x00B43D54's 14: (cell x, cell z), ret 8: the first node
inline constexpr std::size_t kIsType = 0x24;        // a node's object's 9: (type), ret 4, a bool
inline constexpr std::size_t kUDriveOn = 0x14;      // the U-Drive service's 5: (0xB), ret 4
inline constexpr std::size_t kUDriveOff = 0x18;     // its 6: (0xB), ret 4
// Part em2b's, of the automaton unless noted.
inline constexpr std::size_t kGetPosition = 0x1C;   // 7: (float* out), ret 4
inline constexpr std::size_t kPopState = 0xD4;      // 53
inline constexpr std::size_t kSlot85 = 0x154;       // 85: a bool
inline constexpr std::size_t kSlot87 = 0x15C;       // 87: a bool (the end of the way)
inline constexpr std::size_t kSlot88 = 0x160;       // 88: a bool
inline constexpr std::size_t kEnterOccupant = 0x210; // 132: (occupant, groups, path, keep), ret 0x10
inline constexpr std::size_t kSlot147 = 0x24C;      // 147: (occupant, byte), ret 8
inline constexpr std::size_t kSlot148 = 0x250;      // 148: an object
inline constexpr std::size_t kSlot165 = 0x294;      // 165: a bool
inline constexpr std::size_t kSlot192 = 0x300;      // 192: a word
inline constexpr std::size_t kTargetPosition = 0x20; // the target's 8: (float* out), ret 4
inline constexpr std::size_t kTrafficNetwork = 0xA0; // the city's 40: the traffic network map
inline constexpr std::size_t kCityPathFinder = 0xAC; // the city's 43: the path finder
inline constexpr std::size_t kOccupantAt = 0x2C;    // the map's 11: (cell x, cell z), ret 8
inline constexpr std::size_t kFindPath = 0x44;      // the path finder's 17: (automaton, from, to, a, b), ret 0x14
inline constexpr std::size_t kMessageType = 0x14;   // a cGZMessage's 5: (type), ret 4
inline constexpr std::size_t kMessageData1 = 0x2C;  // its 11: (word), ret 4
inline constexpr std::size_t kMessageData2 = 0x40;  // its 16: (word), ret 4
inline constexpr std::size_t kMessageObject = 0x84; // its 33: (object), ret 4
inline constexpr std::size_t kPost = 0x10;          // the message server's 4: (message, 0), ret 8
// Part em3a's (the fire truck), of the automaton unless noted.
inline constexpr std::size_t kSetDestination = 0x28; // 10: (point), ret 4
inline constexpr std::size_t kGoHome = 0x74;        // 29
inline constexpr std::size_t kGetSpeed = 0x98;      // 38: a float in ST0
inline constexpr std::size_t kGroups = 0x220;       // 136: a word
inline constexpr std::size_t kReach = 0x304;        // the fire truck's 193: a float in ST0
inline constexpr std::size_t kCitySimulators = 0x120; // the city's 72: the simulators
inline constexpr std::size_t kSimulatorById = 0x1C; // their 7: (class), ret 4
inline constexpr std::size_t kCityTargets = 0xE4;   // the city's 57: a service of targets
inline constexpr std::size_t kTargetKnown = 0x1C;   // its 7: (target), ret 4, a bool
inline constexpr std::size_t kTargetReach = 0x60;   // its 24: (target), ret 4, a float in ST0
inline constexpr std::size_t kCityWidth = 0x174;    // the city's 93: cells across
inline constexpr std::size_t kCityDepth = 0x178;    // the city's 94: cells deep
inline constexpr std::size_t kCityOccupants = 0x88; // the city's 34: the occupant manager
inline constexpr std::size_t kOccupantsInBox = 0x90; // its 36: (list, low, high, filter, -1), ret 0x14
inline constexpr std::size_t kListDone = 0xC0;      // its 48: (list), ret 4
inline constexpr std::size_t kCrewAdd = 0x1C;       // the city's 44 object's 7: (crew), ret 4
inline constexpr std::size_t kPick = 0xF0;          // 0x00B43DD0's 60: (x, y, float* out, 1, 1), ret 0x14
inline constexpr std::size_t kNetworkPart = 0xC4;   // an occupant's 49: its network part
inline constexpr std::size_t kPartReaches = 0x54;   // a network part's 21: (1, point, point, 0), ret 0x10
inline constexpr std::size_t kFireCount = 0x14;     // the fire interface's 5: a count
inline constexpr std::size_t kFiresIn = 0x10;       // its 4: (rect), ret 4: a count
inline constexpr std::size_t kBurning = 0x2C;       // the 0x49B43F57 interface's 11: an object
inline constexpr std::size_t kBurningCount = 0x40;  // that object's 16: a count
inline constexpr std::size_t kServiceHas = 0x1C;    // 0x00B43D78's 7: (object), ret 4, a bool
inline constexpr std::size_t kServiceWord = 0x28;   // its 10: (out, object), ret 8: a pointer to a word
} // namespace slot

// The fire truck's slot 121's argument (the driving controls): a float the
// steering needs at or above zero, two words given to 0x00B43DD0's slot 60
// and the point it writes.
namespace controls {
inline constexpr std::size_t kHeld = 0x9C, kScreen = 0x214, kAim = 0x21C;
} // namespace controls

// A state record's type (the core's states).
namespace state {
inline constexpr std::size_t kType = 0x4;
} // namespace state

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kUint8 = 0x14, kUint16 = 0x1C, kUint32 = 0x24;
} // namespace stream

} // namespace opensc4::automata_emergency
