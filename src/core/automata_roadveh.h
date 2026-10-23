// The layout of the other road vehicles, parts ro1 to ro3a (build 641; functions,
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
// Part ro2's, of the automaton (main vtable) unless noted.
inline constexpr std::size_t kId = 0x14;                // 5: a word
inline constexpr std::size_t kGetPosition = 0x1C;       // 7: (float* out), ret 4
inline constexpr std::size_t kRemove = 0x34;            // 13: (bool), ret 4
inline constexpr std::size_t kDirection = 0x3C;         // 15: (float* out), ret 4
inline constexpr std::size_t kPlace = 0x40;             // 16: (point, heading), ret 8
inline constexpr std::size_t kGetRadius = 0x44;         // 17: a float in ST0
inline constexpr std::size_t kSound = 0x58;             // 22: (key, 1, 1, float), ret 0x10
inline constexpr std::size_t kCheckFlag = 0x7C;         // 31: (flag), a bool
inline constexpr std::size_t kFadeNow = 0x80;           // 32: a float in ST0
inline constexpr std::size_t kFade = 0x84;              // 33: (float, float), ret 8
inline constexpr std::size_t kStep = 0x98;              // 38: a float in ST0
inline constexpr std::size_t kNextAutomaton = 0xC0;     // 48 (also of the bus stop's chain)
inline constexpr std::size_t kSetLeader = 0xC4;         // 49: (automaton), ret 4; an occupant's: its network part
inline constexpr std::size_t kPushState = 0xD0;         // 52: seven words, ret 0x1C
inline constexpr std::size_t kPopState = 0xD4;          // 53
inline constexpr std::size_t kRecord = 0xE4;            // 57: (word), ret 4
inline constexpr std::size_t kStateType = 0xEC;         // 59
inline constexpr std::size_t kSlot61 = 0xF4;            // 61: (word), ret 4
inline constexpr std::size_t kSlot83 = 0x14C;           // 83: (seconds), ret 4
inline constexpr std::size_t kEnter = 0x210;            // 132: (occupant, groups, path, keep), ret 0x10
inline constexpr std::size_t kGroups = 0x220;           // 136
inline constexpr std::size_t kOccupantOf = 0x224;       // 137
inline constexpr std::size_t kPosition = 0x230;         // 140: the position's address
inline constexpr std::size_t kSlot173 = 0x2B4;          // 173: (a, b), ret 8
inline constexpr std::size_t kManager = 0x2B8;          // 174: the vehicle manager
inline constexpr std::size_t kSlot178 = 0x2C8;          // 178: (float*), ret 4
inline constexpr std::size_t kSlot179 = 0x2CC;          // 179: a float*
inline constexpr std::size_t kSlot180 = 0x2D0;          // 180: (float*), ret 4
inline constexpr std::size_t kRemember = 0x2D4;         // 181: (deque, count), ret 8
inline constexpr std::size_t kSlot184 = 0x2E0;          // 184
inline constexpr std::size_t kPushWord = 0x300;         // the trailer's 192: (word), ret 4
inline constexpr std::size_t kAddCar = 0x30C;           // the trailer's 195
inline constexpr std::size_t kFollowPath = 0x310;       // the trailer's 196: (seconds), ret 4
inline constexpr std::size_t kFollowLeader = 0x314;     // the trailer's 197: (seconds), ret 4
inline constexpr std::size_t kCrossPath = 0x318;        // the trailer's 198: four pointers, ret 0x10
inline constexpr std::size_t kRemoveHighlight1 = 0x304; // the tank's 193
inline constexpr std::size_t kRemoveHighlight2 = 0x310; // the tank's 196
inline constexpr std::size_t kMakeVehicle = 0x3C;       // the vehicle manager's 15: seven words, ret 0x1C
// Part ro3a's, of the automaton unless noted.
inline constexpr std::size_t kSlot18 = 0x48;           // 18: a bool (the tank's slot 121 then only steers)
inline constexpr std::size_t kControls = 0xE0;         // 56: the U-Drive controls, or null
inline constexpr std::size_t kTemplate = 0x10C;        // 67: the template
inline constexpr std::size_t kSlot100 = 0x190;         // 100: (word, word), ret 8
inline constexpr std::size_t kModelTransform = 0x24;   // the model's virtual base's slot 9: (out), ret 4
inline constexpr std::size_t kAttachPlace = 0x10;      // the attachments' slot 4: (point, heading, word), ret 0xC
inline constexpr std::size_t kShotVelocity = 0x2C;     // the shot effect's slot 11: (0xF, float*, 1), ret 0xC
inline constexpr std::size_t kShotSpeed = 0x30;        // the shot effect's slot 12: (0, float*, 1), ret 0xC
inline constexpr std::size_t kShotStart = 0x0C;        // the shot effect's slot 3: (0), ret 4
inline constexpr std::size_t kPick = 0x90;             // 0x00B43DC4's slot 36: (x, y, out, 0), ret 0x10
inline constexpr std::size_t kSoundOn = 0x14;          // the U-Drive sounds' (0x00602640) slot 5: (id), ret 4
inline constexpr std::size_t kSoundOff = 0x18;         // their slot 6: (id), ret 4
} // namespace slot

// The U-Drive controls (slot 56's object, part vb8's state) as the tank's
// slots 110, 115 and 121 use them.
namespace udrive {
inline constexpr std::size_t kHeading1 = 0x38, kBack = 0x44, kHeading2 = 0x50; // three floats each
inline constexpr std::size_t kMouseX = 0x7C, kMouseY = 0x80;              // -1 unset
inline constexpr std::size_t kTurnLeft = 0x84, kTurnRight = 0x88;         // -1 unset; zeroed when used
inline constexpr std::size_t kBarrelUp = 0x90, kBarrelDown = 0x94, kFire = 0x98, kTurretKey = 0x9C;
inline constexpr std::size_t kFaster = 0xA0; // held 250 or more: the velocity up 5 (wraps past 100 to 30)
inline constexpr std::size_t kVelocityUp = 0xA8, kVelocityDown = 0xAC;
inline constexpr std::size_t kBarrelUpBefore = 0xC4, kBarrelDownBefore = 0xC8;
inline constexpr std::size_t kVelocityUpBefore = 0xDC, kVelocityDownBefore = 0xE0;
inline constexpr std::size_t kWord114 = 0x114, kWord144 = 0x144, kShots = 0x178, kWord1AC = 0x1AC;
inline constexpr std::size_t kWord1E0 = 0x1E0, kFireByte = 0x1FF, kPointerX = 0x214, kPointerY = 0x218;
inline constexpr std::size_t kByte228 = 0x228;
// The turret's record: +0x248 to +0x268 (slot 110 sets them all, slot 121
// the barrel +0x250, the turn +0x25C and the velocity +0x268).
inline constexpr std::size_t kRecord = 0x248, kRecordBarrel = 0x250, kRecordTurn = 0x25C, kRecordVelocity = 0x268;
} // namespace udrive

// cSC4Vehicle's fields part ro2 reads (src/core/automata_vehicle.h has them
// too).
namespace field {
inline constexpr std::size_t kPosition = 0x40, kHeading = 0x58, kRadius = 0x7C, kAutomataFlags = 0x84;
inline constexpr std::size_t kByte90 = 0x90, kStatesEnd = 0xB8;
inline constexpr std::size_t kOccupant = 0x1EC, kGroups = 0x1F4, kPath = 0x1F8, kPathIndex = 0x1FC;
inline constexpr std::size_t kWord94 = 0x94, kSmoothed = 0x1A0;
inline constexpr std::size_t kFront = 0x264;
inline constexpr std::size_t kDeque = 0x27C; // start (cur, first, last, node), finish; elements of 8 bytes
} // namespace field

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kSint8 = 0x10, kUint16 = 0x1C, kSint32 = 0x20, kUint32 = 0x24, kError = 0x48;
} // namespace stream

} // namespace opensc4::automata_roadveh
