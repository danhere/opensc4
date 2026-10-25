// The layout of the trains, parts tr1 and tr2a (build 641; functions, vtables and data
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
} // namespace slot

// cSC4Vehicle's fields this part reads (src/core/automata_vehicle.h has them
// too).
namespace field {
inline constexpr std::size_t kObjectB0 = 0xB0, kDraw = 0x80, kSmoothed = 0x1A0;
inline constexpr std::size_t kHeading = 0x58, kSpeed = 0x94, kOccupant = 0x1EC, kDirection = 0x1F4, kFront = 0x264;
inline constexpr std::size_t kDeque204 = 0x204, kDeque27C = 0x27C; // start (cur, first, last, node), finish
} // namespace field

// The stream slots (cIGZOStream / cIGZIStream), each one argument.
namespace stream {
inline constexpr std::size_t kUint16 = 0x1C, kUint32 = 0x24;
} // namespace stream

} // namespace opensc4::automata_trains
