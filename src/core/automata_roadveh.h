// The layout of the other road vehicles, part ro1 (build 641; functions,
// vtables and data in data/addresses/automata.toml, the survey in
// docs/automata.md, "Part ro").
//
// Six classes derive from cSC4Vehicle (src/core/automata_vehicle.h), each
// with its own six vtables (+0x00, +0x04, +0x08, +0x0C, +0x20, +0x38 as
// cSC4Vehicle's):
//   - cSC4TrailerVehicle (0x4A5B69ED, factory 0x00514970, constructor
//     0x00556DB0, 0x2E4 bytes from SC4::operator_new, 199 slots)
//   - cSC4Bus (0xAA9F4A3D, 0x005146C0, 0x00524360, 0x2B8 bytes)
//   - cSC4GetawayVan (0xEA863423, 0x005146A0, 0x00533BA0, 0x2B8 bytes)
//   - cSC4CarjackedVehicle (0xCBB32B0A, 0x005146E0, 0x005244C0, 0x2B8 bytes)
//   - cSC4Tank (0xEBE0DCAD, 0x00514700, 0x0054CD90, 0x37C bytes, 198 slots)
//   - cSC4MayorLimo (0x4C0DD224, 0x00514720, 0x0053CEC0, 0x2C4 bytes)
// All but the trailer come from the vehicle pool's allocator (0x0055B970;
// sizes other than 0x2B8 go on to SC4::operator_new).
//
// The trailer's fields (this part names them by what its Write and Read do):
//   +0x2B8..+0x2C0 a vector of words (begin, end, capacity; slot 192 pushes,
//          slot 194 counts)
//   +0x2C4..+0x2CC a vector of the cars it pulls (automata, referenced; slot
//          193 counts, slot 66 asks the last one)
//   +0x2D0 a point (-1, -1, -1 at construction), +0x2DC an occupant
//          (referenced, read back as 0xA821EF94), +0x2E0 a word (not set by
//          the constructor)
// The limo's: +0x2B8 an int (50), +0x2BC and +0x2C0 floats (48, 10); Init
//          sets them from the property 0xEC15654A.
// The tank's:
//   +0x2B8 and +0x2FC two highlight models with their effect parts at +0x2BC
//          and +0x300 (part ad3)
//   +0x340 a byte (firing), +0x344 and +0x348 the mouse point, +0x34C the
//          turret angle, +0x350 the barrel angle (0.2), +0x354 the barrel
//          velocity (65)
//   +0x358 and +0x359 two model indices (-1 at construction; saved)
//   +0x35C and +0x360 the turret and barrel models (held through a virtual
//          base), +0x364 the shot effect (referenced), +0x368 the effect
//          attachments (cSC4NetworkBridgeOccupant's container)
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_roadveh {

inline constexpr std::uint32_t kTrailerClassId = 0x4A5B69ED;
inline constexpr std::uint32_t kBusClassId = 0xAA9F4A3D;
inline constexpr std::uint32_t kVanClassId = 0xEA863423;
inline constexpr std::uint32_t kCarjackedClassId = 0xCBB32B0A;
inline constexpr std::uint32_t kTankClassId = 0xEBE0DCAD;
inline constexpr std::uint32_t kLimoClassId = 0x4C0DD224;

inline constexpr std::size_t kTrailerSize = 0x2E4, kTankSize = 0x37C, kLimoSize = 0x2C4;

namespace trailer {
inline constexpr std::size_t kWords = 0x2B8, kCars = 0x2C4; // vectors: begin, end, capacity
inline constexpr std::size_t kPoint = 0x2D0, kOccupant = 0x2DC, kWord = 0x2E0;
inline constexpr std::uint32_t kCarIid = 0x896E714A; // the cars are read back as cSC4Vehicle
} // namespace trailer

namespace limo {
inline constexpr std::size_t kCount = 0x2B8, kFloat2BC = 0x2BC, kFloat2C0 = 0x2C0;
inline constexpr std::uint32_t kProperty = 0xEC15654A; // Init's float vector
} // namespace limo

namespace tank {
inline constexpr std::size_t kHighlight1 = 0x2B8, kPart1 = 0x2BC, kHighlight2 = 0x2FC, kPart2 = 0x300;
inline constexpr std::size_t kFiring = 0x340, kMouse = 0x344, kTurret = 0x34C, kBarrel = 0x350, kVelocity = 0x354;
inline constexpr std::size_t kIndex1 = 0x358, kIndex2 = 0x359;
inline constexpr std::size_t kTurretModel = 0x35C, kBarrelModel = 0x360, kShot = 0x364, kAttachments = 0x368;
} // namespace tank

// The slots of other objects this part calls.
namespace slot {
inline constexpr std::size_t kSetFlag = 0x78;         // the automaton's 30: (flag, bool), ret 8
inline constexpr std::size_t kRearPoint = 0x108;      // the automaton's 66: (float* out), ret 4
inline constexpr std::size_t kCarStop = 0x34;         // a car's slot 13: (0), ret 4
inline constexpr std::size_t kModelByIndex = 0x18C;   // the core's 99: (index, out), ret 8
inline constexpr std::size_t kMakeHighlight1 = 0x300; // the tank's 192
inline constexpr std::size_t kMakeHighlight2 = 0x30C; // the tank's 195
inline constexpr std::size_t kInitialized = 0x14;     // the occupant part's slot 5
inline constexpr std::size_t kHolder = 0x18;          // the occupant part's slot 6: its property holder
inline constexpr std::size_t kAttach = 0x08;          // the attachments' slot 2: (drawable), ret 4
inline constexpr std::size_t kAttachEffect = 0x24;    // the attachments' slot 9: (effect name, name), ret 8
inline constexpr std::size_t kAttachDone = 0x14;      // the attachments' slot 5
inline constexpr std::size_t kShotStop = 0x10;        // the shot effect's slot 4: (1), ret 4
inline constexpr std::size_t kShotRunning = 0x14;     // the shot effect's slot 5: a bool
} // namespace slot

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kSint8 = 0x10, kUint16 = 0x1C, kSint32 = 0x20, kUint32 = 0x24, kError = 0x48;
} // namespace stream

} // namespace opensc4::automata_roadveh
