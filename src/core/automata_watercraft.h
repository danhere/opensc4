// The layout of the boat, cSC4Watercraft, parts wc1, wc2a, wc2b and wc2c (build 641;
// functions, vtables and data in data/addresses/automata.toml, the survey
// in docs/automata.md, "Part wc").
//
// cSC4Watercraft (0x096E75F5, factory 0x00514820, constructor 0x0056D9A0,
// 0xDC8 bytes from SC4::operator_new) derives from the automata core
// directly (vtable 0x00A95AC0, 129 slots; src/core/automata_vehicle.h) and
// adds no slots. Its six vtables (+0x00, +0x04, +0x08, +0x0C, +0x20, +0x38)
// are 0x00A9DBB8, 0x00A9DB30, 0x00A9DB14, 0x00A9DAB0, 0x00A9DA9C and
// 0x00A9DA8C.
//
// Its fields (the core's end at +0x1E4):
//   +0x1E4, +0x218 the two legs of a ferry route and +0x24C the leg under
//          way, 0x34 bytes each: +0 an occupant (referenced), +4 a byte
//          vector (begin, end, capacity), +0x10 and +0x1C two points (the
//          float 0x00B22A80 at construction), +0x28 a byte, +0x2C a word,
//          +0x30 a byte and +0x31 a byte (the first two legs: the leg is
//          in the watercraft manager's dock table)
//   +0x280 the boat it tows or is towed by (an automaton, referenced),
//          +0x284 an object (referenced)
//   +0x288, +0x289 two bytes (D and Q in the debug text's (Q,D))
//   +0x28C a route iterator (SC4RouteIterator, 0x16 bytes) over the leg
//          under way's byte vector (+0x250)
//   +0x2A4 a vector of 16-byte path points (begin, end, capacity; 11 zeroed
//          at construction, emptied by Init)
//   +0x2B0..+0x2B2, +0x2CA..+0x2CC, +0x2D8 bytes; +0x2B4, +0x2B8, +0x2BC,
//          +0x2C0, +0x2C4, +0x2D0, +0x2D4, +0x2E4 words; +0x2DC the speed
//          and +0x2E0 the turn (floats)
//   +0x2E8 the start point, +0x2F4 the point, +0x300 the start heading,
//          +0x30C the heading (floats; 0x00B22A80 at construction)
//   +0x318 the target (an occupant, referenced), +0x31C a byte vector (the
//          path to it), +0x340 a byte
//   +0x344..+0x3E3 the drive parameters (0x00567310, part wc2a): 40 floats
//          from the template's WatercraftDriveParams (0xA0 bytes, read in
//          another order: +0x20 after +0x24, +0x44 after +0x48, +0x4C
//          after +0x50)
//   +0x3E4 a generator of its own (SC4Random::ConstructSeeded with -1: 624
//          words, +0x9C0 a pointer to the next, +0x9C4 the count)
//   +0xDAC a word
//   +0xDB0..+0xDC4 six effects (referenced): SMLmushroomcloud,
//          boat_grounding, boat_splash, tugropethrow, tugropeconnection and
//          waterski
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_watercraft {

inline constexpr std::uint32_t kClassId = 0x096E75F5;
inline constexpr std::uint32_t kType = 0x897F7EC2; // the core's +0x28
inline constexpr std::size_t kSize = 0xDC8;
inline constexpr std::uint16_t kSaveVersion = 2;

namespace field {
inline constexpr std::size_t kLegA = 0x1E4, kLegB = 0x218, kLeg = 0x24C, kLegSize = 0x34;
inline constexpr std::size_t kLegRef = 0x00, kLegBytes = 0x04, kLegPoints = 0x10, kLegListed = 0x31;
inline constexpr std::size_t kLegByte28 = 0x28, kLegCount = 0x2C, kLegByte30 = 0x30;
inline constexpr std::size_t kTow = 0x280, kRef284 = 0x284;
inline constexpr std::size_t kByte288 = 0x288, kByte289 = 0x289, kRoute = 0x28C;
inline constexpr std::size_t kPath = 0x2A4; // begin, end, capacity of 16-byte points
inline constexpr std::size_t kByte2B0 = 0x2B0, kByte2B1 = 0x2B1, kByte2B2 = 0x2B2;
inline constexpr std::size_t kWord2B4 = 0x2B4, kWord2B8 = 0x2B8, kWord2BC = 0x2BC;
inline constexpr std::size_t kWord2C0 = 0x2C0, kWord2C4 = 0x2C4;
inline constexpr std::size_t kByte2CA = 0x2CA, kByte2CB = 0x2CB, kByte2CC = 0x2CC;
inline constexpr std::size_t kWord2D0 = 0x2D0, kWord2D4 = 0x2D4, kByte2D8 = 0x2D8;
inline constexpr std::size_t kSpeed = 0x2DC, kTurn = 0x2E0, kWord2E4 = 0x2E4;
inline constexpr std::size_t kStartPoint = 0x2E8, kPoint = 0x2F4, kStartHeading = 0x300, kHeading = 0x30C;
inline constexpr std::size_t kTarget = 0x318, kTargetPath = 0x31C, kByte340 = 0x340;
inline constexpr std::size_t kDriveParams = 0x344, kDriveParamsSize = 0xA0;
inline constexpr std::size_t kParam394 = 0x394, kParam3D4 = 0x3D4; // two drive parameters (wc2b)
inline constexpr std::size_t kParam350 = 0x350, kParam354 = 0x354, kParam3D0 = 0x3D0; // three more (wc2c)
inline constexpr std::size_t kGenerator = 0x3E4, kWordDAC = 0xDAC;
inline constexpr std::size_t kEffects = 0xDB0, kEffectCount = 6;
// The core's.
inline constexpr std::size_t kType28 = 0x28, kFlags33 = 0x33, kMessages = 0x38, kPosition = 0x40;
inline constexpr std::size_t kHeading58 = 0x58;
} // namespace field

// The watercraft manager's (city slot 46) dock table: a hash table at
// +0xA8C whose buckets are +0xA90..+0xA94; a node is {next, leg occupant,
// the boat of the first leg, the boat of the second}. The debug text reads
// +0xA9C and +0xAC4.
namespace manager {
inline constexpr std::size_t kBuckets = 0xA90, kBucketsEnd = 0xA94;
inline constexpr std::size_t kNodeKey = 0x4, kNodeFirst = 0x8, kNodeSecond = 0xC;
inline constexpr std::size_t kCountA9C = 0xA9C, kCountAC4 = 0xAC4;
} // namespace manager

// An automata group of the script system (the boat's slot 67): its
// drive parameters (+0x60, null when it has none) and its parent (+0xC).
namespace group {
inline constexpr std::size_t kParent = 0x0C, kDriveParams = 0x60;
} // namespace group

// A state record of the automata core (slots 110 and 111): its type, and the
// fields the boat's slot 110 sets for the type 0x10.
namespace state {
inline constexpr std::size_t kType = 0x04;
inline constexpr std::size_t kWord30 = 0x30, kWord34 = 0x34, kWord144 = 0x144, kWord178 = 0x178, kWord1AC = 0x1AC;
inline constexpr std::size_t kWord234 = 0x234;
} // namespace state

// The slots this part calls.
namespace slot {
inline constexpr std::size_t kInitialized = 0x14;   // the occupant part's slot 5, a bool
inline constexpr std::size_t kRemove = 0x34;        // the automaton's 13: (bool), ret 4
inline constexpr std::size_t kGetPosition = 0x3C;   // an occupant's 15: (float* out), ret 4
inline constexpr std::size_t kSetFlag = 0x78;       // the automaton's 30: (flag, bool), ret 8
inline constexpr std::size_t kLength = 0x44;        // 17: ST0 (the bow and stern points)
inline constexpr std::size_t kSlot31 = 0x7C;        // 31: (1), ret 4, AL
inline constexpr std::size_t kPushState = 0xD0;     // 52: seven words, ret 0x1C
inline constexpr std::size_t kSlot53 = 0xD4;        // 53: the tow's before it is let go of; the boat's own in ReachLeg
inline constexpr std::size_t kKeys = 0xE0;          // 56: the keys record (+0x7C, +0x80 floats, -1.0 when up)
inline constexpr std::size_t kStateType = 0xEC;     // 59: the current state's type
inline constexpr std::size_t kTemplate = 0x10C;     // 67: the automata group
inline constexpr std::size_t kSlot77 = 0x134;       // 77: (bool), ret 4
inline constexpr std::size_t kSlot102 = 0x198;      // 102: (0xB, 0), ret 8
inline constexpr std::size_t kSlot104 = 0x1A0;      // 104, after the placement
inline constexpr std::size_t kSlot107 = 0x1AC;      // 107, after the placement
inline constexpr std::size_t kCityWatercraft = 0xB8; // the city's 46: the watercraft manager
inline constexpr std::size_t kCityEffects = 0x154;   // the city's 85: the effects manager
inline constexpr std::size_t kCellOf = 0x17C;        // the city's 95: (x, z, int*, int*), ret 0x10
inline constexpr std::size_t kInCity = 0x18C;        // the city's 99: (cell x, cell z), ret 8, AL
inline constexpr std::size_t kBox = 0x28;            // the occupant part's (+4) 10: (float min[3], float max[3]), ret 8
inline constexpr std::size_t kCells = 0x2C;          // the occupant part's (+4) 11: (int out[4]), ret 4
inline constexpr std::size_t kHeightAt = 0x48;       // the terrain's 18: (cell x, cell z), ret 8, ST0
inline constexpr std::size_t kWaterLevel = 0x74;     // the terrain's 29: ST0
inline constexpr std::size_t kMakeEffect = 0x1C;     // the effects manager's 7: (name, effect*), ret 8
inline constexpr std::size_t kEffectStop = 0x10;     // an effect's 4: (1), ret 4
inline constexpr std::size_t kRegister = 0x14;       // the message server's 5: (target, message), ret 8
inline constexpr std::size_t kUnregister = 0x18;     // the message server's 6: (target, message), ret 8
inline constexpr std::uint32_t kMessage = 0xAC400A31; // the message the boat listens to
} // namespace slot

} // namespace opensc4::automata_watercraft
