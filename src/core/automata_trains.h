// The layout of the trains, parts tr1, tr2a and tr2b (build 641; functions, vtables and data
// in data/addresses/automata.toml, the survey in docs/automata.md, "Part tr").
//
// Three classes, each on the one before, with their own six vtables (+0x00,
// +0x04, +0x08, +0x0C, +0x20, +0x38 as cSC4Vehicle's):
//   - cSC4TrainCar (0x4A416254, factory 0x00514910, constructor 0x0055B150,
//     0x2C8 bytes from its own pool 0x00B0A09C, 193 slots: slot 192
//     0x00648EF0 answers false, the train's true) on cSC4Vehicle
//     (src/core/automata_vehicle.h)
//   - cSC4Train (0x8A41625A, 0x00514940, 0x0055B2F0, 0x2FC bytes from
//     SC4::operator_new, 200 slots: adds 192 to 199)
//   - cSC4MassTransitTrain (0x4C22AE53, 0x00514760, 0x0053CBB0, 0x2FC bytes,
//     200 slots)
//
// The car's fields:
//   +0x2B8 a byte (Init: the occupant is in group 0x4217 or 0x4208)
//   +0x2BC..+0x2C4 three floats (the float 0x00B22964 at construction; not
//          saved)
// The train's:
//   +0x2C8 the wanted number of cars (a word; slot 195 sets it, slot 196
//          answers its low byte; saved)
//   +0x2CC the cars' occupant group (Init: 0x4203, 0x4201, 0x4208, 0x422D or
//          0x422F by the train's own group; not saved)
//   +0x2D0..+0x2D8 a vector of the cars (automata, referenced; begin, end,
//          capacity; slot 194 counts, slot 193 trims)
//   +0x2DC a point (-1, -1, -1 at construction), +0x2E8 an occupant
//          (referenced, read back as 0xA821EF94), +0x2EC a word (not set by
//          the constructor); all saved
//   +0x2F0 a word (zero at construction), +0x2F4 a word and +0x2F8 a byte
//          (zero from Init)
// The mass transit train adds none.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_trains {

inline constexpr std::uint32_t kCarClassId = 0x4A416254;
inline constexpr std::uint32_t kTrainClassId = 0x8A41625A;
inline constexpr std::uint32_t kMassTransitClassId = 0x4C22AE53;

inline constexpr std::size_t kCarSize = 0x2C8, kTrainSize = 0x2FC;

namespace car {
inline constexpr std::size_t kGroupByte = 0x2B8, kFloats = 0x2BC;
} // namespace car

namespace train {
inline constexpr std::size_t kWanted = 0x2C8, kCarGroup = 0x2CC, kCars = 0x2D0; // kCars: begin, end, capacity
inline constexpr std::size_t kPoint = 0x2DC, kOccupant = 0x2E8, kWord = 0x2EC;
inline constexpr std::size_t kWord2F0 = 0x2F0, kWord2F4 = 0x2F4, kByte2F8 = 0x2F8;
} // namespace train

// The slots of other objects this part calls.
namespace slot {
inline constexpr std::size_t kInitialized = 0x14;  // the occupant part's slot 5
inline constexpr std::size_t kInGroup = 0x58;      // the occupant part's slot 22: (group), a bool
inline constexpr std::size_t kSetFlag = 0x78;      // the automaton's 30: (flag, bool), ret 8
inline constexpr std::size_t kCarStop = 0x34;      // a car's slot 13: (0), ret 4
inline constexpr std::size_t kSlot50 = 0xC8;       // 50: no arguments
inline constexpr std::size_t kStateType = 0xEC;    // 59: the current state's type
inline constexpr std::size_t kCollide = 0x104;     // 65: (a, b), ret 8
inline constexpr std::size_t kControls = 0xE0;     // 56: the U-Drive controls
inline constexpr std::size_t kTrimCars = 0x304;    // the train's 193: (count), ret 4
inline constexpr std::size_t kSetCarCount = 0x30C; // the train's 195: (count), ret 4
inline constexpr std::size_t kCell = 0xEC;         // an occupant's 59: (int* out, int* out), ret 8
inline constexpr std::size_t kLeaveCell = 0x9C;    // 0x00B43D54's 39: (automaton, a, b), ret 0xC
inline constexpr std::size_t kDisasterActive = 0x28; // 0x00B43D3C's 10: a bool
// Part tr2a's, of the automaton unless noted.
inline constexpr std::size_t kId = 0x14;            // 5: a word
inline constexpr std::size_t kRecordSlot13 = 0x34;  // a state's +0x28 object's 13: (kind, word*, 1), ret 0xC
inline constexpr std::size_t kCheckFlag = 0x7C;     // 31: (flag), a bool
inline constexpr std::size_t kFade = 0x84;          // 33: (float, float), ret 8
inline constexpr std::size_t kSetSpeed = 0x90;      // 36: (target, rate), ret 8
inline constexpr std::size_t kNextAutomaton = 0xC0; // 48
inline constexpr std::size_t kPushState = 0xD0;     // 52: seven words, ret 0x1C
inline constexpr std::size_t kPopState = 0xD4;      // 53
inline constexpr std::size_t kTemplate = 0x10C;     // 67: the template
inline constexpr std::size_t kSlot173 = 0x2B4;      // 173: (a, b), ret 8
inline constexpr std::size_t kRemember = 0x2D4;     // 181: (deque, count), ret 8
inline constexpr std::size_t kIsTrain = 0x300;      // 192: the car's 0x00648EF0 (false), the train's 0x00648F00 (true)
inline constexpr std::size_t kNetworkKind = 0x18C;  // the occupant's 0x4A10AFC2 interface's 99: (word*), a word
inline constexpr std::size_t kEnterCell = 0x98;     // 0x00B43D54's 38: (automaton, a, b), ret 0xC
inline constexpr std::size_t kCellOf = 0x17C;       // the city's 95: (x, z, int*, int*), ret 0x10
inline constexpr std::size_t kGetLot = 0x18;        // the lot manager's 6: (x, z, 0), ret 0xC
// Part tr2b's, of the automaton unless noted.
inline constexpr std::size_t kRemove = 0x34;          // 13: (bool), ret 4
inline constexpr std::size_t kPlace = 0x40;           // 16: (point, heading), ret 8
inline constexpr std::size_t kGetRadius = 0x44;       // 17: a float in ST0
inline constexpr std::size_t kSmoothedNow = 0x88;     // 34: a float in ST0
inline constexpr std::size_t kSetLeader = 0xC4;       // 49: (automaton), ret 4; an occupant's: its network part
inline constexpr std::size_t kSlot79 = 0x13C;         // 79
inline constexpr std::size_t kSlot88 = 0x160;         // 88: a bool
inline constexpr std::size_t kSlot102 = 0x198;        // 102: (word, word), ret 8
inline constexpr std::size_t kSlot125 = 0x1F4;        // 125: a float in ST0
inline constexpr std::size_t kPathTo = 0x20C;         // 131: (occupant, key, exact), ret 0xC
inline constexpr std::size_t kEnterOccupant = 0x210;  // 132: (occupant, groups, path, keep), ret 0x10
inline constexpr std::size_t kAdvanceOnPath = 0x214;  // 133: six words, ret 0x18
inline constexpr std::size_t kClearAhead = 0x25C;     // 151: a float in ST0
inline constexpr std::size_t kSlot161 = 0x284;        // 161
inline constexpr std::size_t kBestDirection = 0x2AC;  // 171: (occupant, direction, side*, 1), ret 0x10
inline constexpr std::size_t kManager = 0x2B8;        // 174: the vehicle manager
inline constexpr std::size_t kTrail = 0x2D8;          // 182: (point, out, distance, deque), ret 0x10, a bool
inline constexpr std::size_t kTrafficNetwork = 0xA0;  // the city's 40
inline constexpr std::size_t kOccupantAt = 0x2C;      // the traffic network map's 11: (cell x, cell z), ret 8
inline constexpr std::size_t kPartPath = 0x0C;        // a network part's 3: (key, word), ret 8
inline constexpr std::size_t kFindPath = 0x14;        // a network part's 5: six words, ret 0x18
inline constexpr std::size_t kKeys = 0x18;            // a network part's 6: six words, ret 0x18
inline constexpr std::size_t kIndexOf = 0x4C;         // a network part's 19: (point, path), ret 8
inline constexpr std::size_t kNeighbour = 0x3C;       // 0x00B43D54's 15: (occupant, side, word, kind), ret 0x10
inline constexpr std::size_t kNeighbourKey = 0xA0;    // 0x00B43D54's 40: (occupant, side), ret 8
inline constexpr std::size_t kEventAt = 0x44;         // 0x00B43DA8's 17: (cell x, cell z), ret 8, a bool
inline constexpr std::size_t kMakeCar = 0x3C;         // the vehicle manager's 15: seven words, ret 0x1C
inline constexpr std::size_t kSetType = 0x14;         // a message's 5: (type), ret 4
inline constexpr std::size_t kSetData1 = 0x2C;        // a message's 11: (word), ret 4
inline constexpr std::size_t kSetData2 = 0x40;        // a message's 16: (word), ret 4
inline constexpr std::size_t kPost = 0x10;            // the message server's 4: (message, 0), ret 8
} // namespace slot

// cSC4Vehicle's fields this part reads (src/core/automata_vehicle.h has them
// too).
namespace field {
inline constexpr std::size_t kObjectB0 = 0xB0, kDraw = 0x80, kSmoothed = 0x1A0;
inline constexpr std::size_t kHeading = 0x58, kSpeed = 0x94, kOccupant = 0x1EC, kDirection = 0x1F4, kFront = 0x264;
inline constexpr std::size_t kDeque204 = 0x204, kDeque27C = 0x27C; // start (cur, first, last, node), finish
// Part tr2b's.
inline constexpr std::size_t kPosition = 0x40, kDestination = 0x4C, kRadius = 0x7C, kAutomataFlags = 0x84;
inline constexpr std::size_t kByte90 = 0x90, k98 = 0x98, kVectorA4 = 0xA4, kNetwork = 0x1F0, kKind1F7 = 0x1F7;
inline constexpr std::size_t kPath = 0x1F8, kPathIndex = 0x1FC, kRoute = 0x22C, kFloat260 = 0x260;
inline constexpr std::size_t kTravel = 0x270; // the direction of travel (+0x264 the front point)
inline constexpr std::size_t kRouteFinish = 0x23C; // the route deque's finish current (its start's at +0x22C)
} // namespace field

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kUint16 = 0x1C, kUint32 = 0x24;
} // namespace stream

} // namespace opensc4::automata_trains
