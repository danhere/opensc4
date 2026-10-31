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
//   +0x268 the bump's timer, a float (zero; not saved)
//   (+0x258 and +0x25C are the push: set by another pedestrian's bump,
//   used and cleared by the walks)
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

// Part ped2b: the walks' fields, slots and objects.
namespace walk {
inline constexpr std::size_t kHeading = 0x58, kObjectB0 = 0xB0, kPlaced = 0x1F8;
// The state 8's record: the cell it last found a side on and the side.
inline constexpr std::size_t kCellX = 0x28, kCellZ = 0x2C, kSide = 0x30;
inline constexpr std::size_t kNames = 0x24; // the state 3's record: the names (+0x10 begin, +0x14 end; 0x14 bytes each)
// Another pedestrian's push as the bump sets it through its occupant part
// (+4): the byte +0x258 and the point +0x25C.
inline constexpr std::size_t kOtherByte = 0x254, kOtherPoint = 0x258;
inline constexpr std::uint32_t kCanGoKind = 0xC49;       // the traffic network map's slot 44 third argument
inline constexpr std::uint32_t kNearbyCount = 10;        // the bump's slot 37 count
inline constexpr std::uint32_t kRunTarget = 0x3B360B61;  // 1/360: the state 9's smoothed speed
inline constexpr std::uint32_t kTargetIid = 0xA9B40F05;  // +0xB0 as the state 9 asks for it
} // namespace walk

namespace wslot {
inline constexpr std::size_t kAsOccupant = 0x14;      // 5: the occupant part (+4)
inline constexpr std::size_t kRemove = 0x34;          // 13: (bool), ret 4
inline constexpr std::size_t kPlace = 0x40;           // 16: 0x00544530 (ped1), (point, heading), ret 8
inline constexpr std::size_t kSmoothedNow = 0x88;     // 34: a float in ST0
inline constexpr std::size_t kSlot87 = 0x15C;         // 87: the flag 4 test (the end of the way)
inline constexpr std::size_t kSlot125 = 0x1F4;        // 125: a float in ST0
inline constexpr std::size_t kMoveOnPath = 0x204;     // 129: (seconds), ret 4
inline constexpr std::size_t kPedestrianManager = 0x2B8; // 174: 0x00544D90 (ped1), the city's 44
// Other objects'.
inline constexpr std::size_t kCityNetwork = 0xA0;     // the city's 40: the traffic network map
inline constexpr std::size_t kCellOf = 0x17C;         // the city's 95: (x, z, int* cell x, int* cell z), ret 0x10
inline constexpr std::size_t kCellList = 0x38;        // the map's 14: (cell x, cell z), ret 8: the cell's first node or null
inline constexpr std::size_t kNeighbour = 0x3C;       // the map's 15: (occupant, side, 0, 0), ret 0x10: an occupant's address
inline constexpr std::size_t kAutomataOn = 0x40;      // the map's 16: (occupant), ret 4
inline constexpr std::size_t kCanGo = 0xB0;           // the map's 44: (from, to, kind), ret 0xC (not read)
inline constexpr std::size_t kCellOpen = 0x74;        // 0x00B43D54's 29: (x, z, 0xFF, side, 2), ret 0x14, a bool
inline constexpr std::size_t kOccupantsInBox = 0x94;  // 0x00B43D0C's 37: (list, low, high, type, count), ret 0x14
inline constexpr std::size_t kListDone = 0xC0;        // its 48: (list), ret 4
inline constexpr std::size_t kGetLot = 0x18;          // the lot manager's 6: (cell x, cell z, bool), ret 0xC
inline constexpr std::size_t kRect = 0x38;            // a lot's 14: (int rect out), ret 4
inline constexpr std::size_t kObjectRect = 0x2C;      // the state 6's object's word's 11: (int rect out), ret 4
inline constexpr std::size_t kOccupantPoint = 0x20;   // an occupant's 8: (float* out), ret 4
inline constexpr std::size_t kOccupantObject = 0xC0;  // an occupant's 48: an object whose slot 8 gives a point
inline constexpr std::size_t kTargetPoint = 0x1C;     // +0xB0's 7: (float* out), ret 4
} // namespace wslot

// Part ped3: the one-shot's and the riot police's slots and objects.
namespace cslot {
inline constexpr std::size_t kSlot83 = 0x14C;         // 83: the core's 0x005091C0, (seconds), ret 4
inline constexpr std::size_t kFade = 0x84;            // 33: the core's 0x0050B360, (float, float), ret 8
inline constexpr std::size_t kPart20Set = 0x0C;       // the +0x20 part's 3: (word), ret 4
// The occupant part's (+4) property holder and the one-shot's count range.
inline constexpr std::size_t kPropertyHolder = 0x18;  // the occupant part's 6: an object
inline constexpr std::size_t kGetProperty = 0x24;     // the holder's 9: (id), ret 4: the property or null
inline constexpr std::size_t kPropertyValue = 0x14;   // the property's 5: its value
inline constexpr std::size_t kValueBytes = 0x17C;     // the value's 95: its bytes
inline constexpr std::size_t kValueCount = 0x10;      // the value's 4: their count
// The car (+0x270).
inline constexpr std::size_t kCarPosition = 0x1C;     // 7: (float* out), ret 4
inline constexpr std::size_t kCarTransform = 0x30;    // 12: (transform* out), ret 4
inline constexpr std::size_t kCarHeading = 0x3C;      // 15: (float* out), ret 4
inline constexpr std::size_t kMission = 0x68;         // 26: the emergency vehicle's mission
inline constexpr std::size_t kCarOccupant = 0x224;    // 137: +0x1EC
inline constexpr std::size_t kCarPoint = 0x23C;       // 143: the second point's address
// The city's and others'.
inline constexpr std::size_t kCitySounds = 0xBC;      // the city's 47: the sounds' service
inline constexpr std::size_t kSoundsMake = 0x14;      // its 5: (occupant, SC4String*, 0), ret 0xC: a sound
inline constexpr std::size_t kCityCellsX = 0x174;     // the city's 93: an int
inline constexpr std::size_t kCityCellsZ = 0x178;     // the city's 94: an int
inline constexpr std::size_t kMapHeight = 0x48;       // the city's slot 40 map's 18: (x, z), ret 8, a float in ST0
inline constexpr std::size_t kManagerFind = 0xB0;     // slot 174's object's 44: (key*), ret 4: an object (its +0x30 a word)
inline constexpr std::size_t kMessageType = 0x14;     // a cGZMessage's 5: (type), ret 4
inline constexpr std::size_t kMessageData1 = 0x2C;    // its 11: (word), ret 4
inline constexpr std::size_t kMessageData2 = 0x40;    // its 16
inline constexpr std::size_t kMessageData3 = 0x54;    // its 21
inline constexpr std::size_t kMessageData4 = 0x68;    // its 26
inline constexpr std::size_t kMessageObject = 0x84;   // its 33: (object), ret 4
inline constexpr std::size_t kPost = 0x10;            // the message server's 4: (message, 0), ret 8
// The camera view's (0x00B43DD8) direction, three floats.
inline constexpr std::size_t kCameraDirection = 0x54;
} // namespace cslot

namespace crewdata {
inline constexpr std::uint32_t kRepulsorMessage = 0xA9E3B6E2; // the riot police's Init (at level 2) and Shutdown
inline constexpr std::uint32_t kFrameMessage = 0x8A0F3F7B;    // the riot police's frame with the automata flag 1
inline constexpr std::uint32_t kDispatchMessage = 0xAA0F3F75; // the dispatch on the mission 5
inline constexpr std::uint32_t kLevelKeyType = 0x29A5D1EC, kLevelKeyGroup = 0x49A593E7; // the mission 5's start model
inline constexpr std::uint32_t kDrawnKeyType = 0x5AD0E817, kDrawnKeyGroup = 0xBADB57F1; // the mission 1's models
inline constexpr std::uint32_t kCountProperty = 0xEA958845; // the one-shot's count range (two bytes)
inline constexpr std::uint32_t kMessageSize = 0x2C;           // a cGZMessage
} // namespace crewdata

} // namespace opensc4::automata_pedestrians
