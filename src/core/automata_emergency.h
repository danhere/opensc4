// The layout of the emergency vehicles, part em1 (build 641; functions, vtables and data in
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
} // namespace slot

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kUint8 = 0x14, kUint16 = 0x1C, kUint32 = 0x24;
} // namespace stream

} // namespace opensc4::automata_emergency
