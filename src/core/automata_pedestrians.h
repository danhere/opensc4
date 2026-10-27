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

} // namespace opensc4::automata_pedestrians
