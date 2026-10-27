// The layout of the pedestrians, part ped (build 641; functions, vtables and data in
// data/addresses/automata.toml, the survey in docs/automata.md, "Part ped").
//
// cSC4Pedestrian on the automaton base (src/core/automata_vehicle.h) and three
// classes on it, each with its own six vtables (+0x00, +0x04, +0x08, +0x0C,
// +0x20, +0x38 as the base's):
//   - cSC4Pedestrian (0x896E75AF, factory 0x005149A0, constructor 0x00545F20,
//     0x270 bytes from the pool 0x00B0A018, main vtable 0x00A9A718, 177
//     slots: adds 176)
//   - cSC4PedestrianOneShot (0xAA09DC28, 0x005149D0, 0x00546860, 0x274
//     bytes, 179 slots: adds 177 and 178)
//   - cSC4RiotPolice (0x8A6E4FA4, 0x005147A0, 0x00547800, 0x27C bytes, 177
//     slots)
//   - cSC4SimFireCrew (0x4A222E4D, 0x00514780, 0x0054A2A0, 0x2C4 bytes, 177
//     slots)
// The pedestrian's allocator (0x00544890) gives 0x270 bytes from the pool and
// any other size from SC4::operator_new; 0x00545130 gives them back the same
// way. The riot police's and the fire crew's main slots 1 and 2 are
// 0x0065A860 / 0x0065A870 (AddRef and Release of another base).
//
// The pedestrian's fields:
//   +0x254 a word, +0x258 a byte, +0x25C a point (three copies of the float
//          0x00B228DC at construction), +0x26C a float (0.01): all saved
//   +0x268 a word (zero; not saved)
// The one-shot's:
//   +0x270 a byte (5; slots 177 and 178; not saved)
// The riot police's:
//   +0x270 the patrol car (referenced; read back as 0xA9B40F05), +0x274 a
//          float (0x00B228E0's bits), +0x278 a byte (1), +0x279 a byte: saved
//   +0x27A a byte (zero; not saved)
// The fire crew's:
//   +0x270, +0x274 and +0x278 as the riot police's (the fire truck, the word
//          0x00B228EC's bits); saved
//   +0x27C, +0x280, +0x290, +0x294 objects released by the destructor,
//   +0x284 a block it frees (SC4::Free), +0x288, +0x28C words: zero, not saved
//   +0x298 and +0x29C two occupant filters (the fire truck's vtables), not
//          saved
//   +0x2A0 a word, +0x2A4 a byte, +0x2A8 a point (zero), +0x2B4 a float
//          (not set at construction), +0x2B8 a point (three copies of the
//          float 0x00B228F0): saved
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_pedestrians {

inline constexpr std::uint32_t kPedestrianClassId = 0x896E75AF;
inline constexpr std::uint32_t kOneShotClassId = 0xAA09DC28;
inline constexpr std::uint32_t kRiotPoliceClassId = 0x8A6E4FA4;
inline constexpr std::uint32_t kFireCrewClassId = 0x4A222E4D;

inline constexpr std::size_t kPedestrianSize = 0x270, kOneShotSize = 0x274, kRiotPoliceSize = 0x27C,
                             kFireCrewSize = 0x2C4;

inline constexpr std::uint32_t kPedestrianType = 0x496E5783; // the occupant type at +0x28

namespace ped {
inline constexpr std::size_t kWord = 0x254, kByte = 0x258, kPoint = 0x25C, kWord268 = 0x268, kFloat = 0x26C;
} // namespace ped

namespace oneshot {
inline constexpr std::size_t kByte = 0x270;
} // namespace oneshot

// The riot police's and the fire crew's shared fields.
namespace crew {
inline constexpr std::size_t kVehicle = 0x270, kWord = 0x274, kByte = 0x278;
} // namespace crew

namespace riot {
inline constexpr std::size_t kByte279 = 0x279, kByte27A = 0x27A;
} // namespace riot

namespace fire {
inline constexpr std::size_t kHeld27C = 0x27C, kHeld280 = 0x280, kBlock = 0x284, kWord288 = 0x288, kWord28C = 0x28C;
inline constexpr std::size_t kHeld290 = 0x290, kHeld294 = 0x294, kFilter = 0x298, kFilter2 = 0x29C;
inline constexpr std::size_t kWord2A0 = 0x2A0, kByte2A4 = 0x2A4, kPoint2A8 = 0x2A8, kFloat2B4 = 0x2B4,
                             kPoint2B8 = 0x2B8;
} // namespace fire

inline constexpr std::uint32_t kVehicleIid = 0xA9B40F05; // the crews' +0x270 is read back as this

inline constexpr std::uint16_t kPedestrianVersion = 5; // the pedestrian's Write and Read
inline constexpr std::uint16_t kCrewVersion = 2;       // the riot police's and the fire crew's

// The slots of other objects this part calls.
namespace slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08;
inline constexpr std::size_t kCityCrewService = 0xB0; // the city's 44
inline constexpr std::size_t kStreamError = 0x48;     // a stream's 18: its error (AL tested)
} // namespace slot

// Part ped2a: the automaton base's and the core's fields the states and
// frames use.
namespace base {
inline constexpr std::size_t kPosition = 0x40, k98 = 0x98, kFloatA0 = 0xA0, kVectorA4 = 0xA4;
inline constexpr std::size_t kStates = 0xB4, kStatesEnd = 0xB8; // the state stack (pointers)
inline constexpr std::size_t kModels = 0x110, kModelsEnd = 0x114, kList188 = 0x188;
inline constexpr std::size_t kSmoothed = 0x1A0, kTimer1C0 = 0x1C0, kOccupant = 0x1EC, kFloat200 = 0x200;
} // namespace base

// A state record: its type, and the object some types hold.
namespace state {
inline constexpr std::size_t kType = 0x04, kObject = 0x08;
inline constexpr std::uint32_t kBurn = 0xC; // the type slot 109's 0x00545280 looks for down the stack
} // namespace state

// The pedestrian's own slots (and the core's and the base's it calls).
namespace pslot {
inline constexpr std::size_t kSetHeading = 0x38;      // 14: the core's 0x0050AF10, (const float*), ret 4
inline constexpr std::size_t kSwapStartModel = 0x4C;  // 19: the core's 0x00510240, (offset), ret 4
inline constexpr std::size_t kSlot21 = 0x54;          // 21: the core's 0x00505880, (const string*), ret 4
inline constexpr std::size_t kSetFlag = 0x78;         // 30: (flag, on), ret 8
inline constexpr std::size_t kCheckFlag = 0x7C;       // 31: (flag), ret 4, a bool
inline constexpr std::size_t kSetState = 0xCC;        // 51: the core's 0x00509860, seven words, ret 0x1C
inline constexpr std::size_t kPopState = 0xD4;        // 53: the core's 0x0050B660
inline constexpr std::size_t kCurrentState = 0xE0;    // 56: the core's 0x005098E0, the current state or null
inline constexpr std::size_t kFlag2 = 0x158;          // 86: the core's 0x004FAD90, the flag 2 test
inline constexpr std::size_t kSlot102 = 0x198;        // 102: the core's 0x0050B540, two words, ret 8
inline constexpr std::size_t kSlot105 = 0x1A4;        // 105: the core's 0x005096B0
inline constexpr std::size_t kPathTo = 0x20C;         // 131: 0x0054E920, (occupant, key, exact), ret 0xC
inline constexpr std::size_t kPlaceOnNetwork = 0x218; // 134: 0x00550B60
inline constexpr std::size_t kGroups = 0x220;         // 136: 0x00504F70, +0x1F4 (its low byte the side)
inline constexpr std::size_t kSlot158 = 0x278;        // 158: 0x00544620, the draw into +0x200
inline constexpr std::size_t kBestDirection = 0x2AC;  // 171: 0x0054D8D0, (network, direction, byte* out, check), ret 0x10
inline constexpr std::size_t kIdle = 0x2C0;           // 176: 0x005457C0
inline constexpr std::size_t kIsInitialised = 0x14;   // the occupant part's (+4) 5
// Other objects'.
inline constexpr std::size_t kNetworkPart = 0xC4;     // an occupant's 49: its network part
inline constexpr std::size_t kFindPath = 0x14;        // a network part's 5: six words, ret 0x18, a path or null
inline constexpr std::size_t kModelSlot14 = 0x38;     // a model's 14: (word), ret 4
inline constexpr std::size_t kModelAdvance = 0x3C;    // a model's 15: (seconds), ret 4
inline constexpr std::size_t kModelFrame = 0x2C;      // a model's 11: its frame, an int
inline constexpr std::size_t kModelFrames = 0x30;     // a model's 12: its frame count, an int
inline constexpr std::size_t kSimulator3C = 0x3C, kSimulator44 = 0x44; // 0x00B43D5C's 15 and 17, bools
inline constexpr std::size_t kStateObjectFrame = 0x70; // a state's object's 28: (automaton, type, seconds), ret 0xC
inline constexpr std::size_t kStateObjectPoint = 0x2C; // a state's object's 11: (float* out), ret 4
inline constexpr std::size_t kStateObjectWord = 0x18;  // a state's object's 6: a word (the state 0xC's)
inline constexpr std::size_t kCitySimulators = 0x120;  // the city's 72: the simulators
inline constexpr std::size_t kSimulatorById = 0x1C;    // the simulators' 7: (id), ret 4
inline constexpr std::size_t kBurnSet = 0x10;          // the 0x49B43F57 interface's 4: (word), ret 4
inline constexpr std::size_t kBurnMode = 0x24;         // its 9: (word), ret 4
} // namespace pslot

inline constexpr std::uint32_t kBurnSimulator = 0x621CDA33, kBurnInterface = 0x49B43F57; // 0x00545280's
inline constexpr std::uint32_t kSmoothTarget = 0x3AB60B61; // the float target (1/720) slot 110 gives SetSmoothedTarget

} // namespace opensc4::automata_pedestrians
