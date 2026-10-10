// Shared between the two parts of our cSC4PropDeveloper (build 641): the
// object part (src/core/prop_object.cpp: everything below 0x006F3620) and
// the props part (src/core/prop_props.cpp: Read, the optional props and
// DoMessage). The prop developer keeps the props that change with the time
// of day, the lot's state or the wealth around them, chooses the optional
// props of new lots through its requesters and shows or hides signs and
// labels.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::prop_sim {

// Fields of cSC4PropDeveloper (offsets from the object's start; 0xA70
// bytes; constructor 0x006F3410, factory 0x006F3600). Names are ours. The
// interfaces: +0x0 the primary cISC4PropDeveloper (0x00AB29F8, 12 slots),
// +0x4 cIGZMessageTarget2 (0x00AB29E8, DoMessage 0x006F4E90), +0x8
// cIGZSerializable (0x00AB29D0: Write 0x006F1930, Read 0x006F3620,
// GetGZCLSID 0x006F0D70). DoMessage reads these 4 lower, Write and Read 8.
namespace field {
constexpr std::size_t kInitialized = 0xC;     // uint8: Init sets it first, Shutdown clears it first
constexpr std::size_t kRefCount = 0x10;       // uint32 (unsigned compare in Release)
constexpr std::size_t kOptionalState = 0x14; // uint8: 0 until Init sets the cell iterator to the city's tracts (1); the props part moves it on (2 in saves); Shutdown clears it; saved
constexpr std::size_t kIterator = 0x18;       // cell iterator (0x20 bytes: state byte, x, z, step, rectangle); saved
constexpr std::size_t kRandom = 0x38;         // generator (0x9C8 bytes), seeded from the time of day * 28
constexpr std::size_t kRequesterAdded = 0xA00; // uint8: the wealth requester is in the vector; saved
constexpr std::size_t kRequesters = 0xA04;    // vector<cISC4PropRequester*>, each referenced; saved
constexpr std::size_t kLots = 0xA10;          // the lot manager (0x00B43D08); saved as a reference
constexpr std::size_t kProps = 0xA14;         // the service 0x00B43D10 (props of a lot, of a rectangle, a prop's record); saved
constexpr std::size_t kLight = 0xA18;         // the service 0x00B43DA0 (slot 37: whether a cell's props are on)
constexpr std::size_t kNetworkLots = 0xA1C;   // the city's slot 42 (+0xA8); saved
constexpr std::size_t kTimed = 0xA20;         // vector of timed props (interface 0xE9793A65), each referenced; saved
constexpr std::size_t kTimedIndex = 0xA2C;    // uint32: the next prop a day or night change switches; saved
constexpr std::size_t kTimedState = 0xA30;    // uint32: 0 idle, 1 turning on (night), 2 turning off; saved
constexpr std::size_t kTimedStep = 0xA34;     // uint32: props per step (an eighth of the vector, rounded up); the constructor leaves it unset; saved
constexpr std::size_t kTimedMs = 0xA38;       // uint32: milliseconds since the last step; saved
constexpr std::size_t kNight = 0xA3C;         // uint8: saved
constexpr std::size_t kThreshold = 0xA40;     // float 0.4: DoMessage's change threshold (message 0x44778231)
constexpr std::size_t kLastValue = 0xA44;     // float -100: the value last acted on
constexpr std::size_t kTable = 0xA48;         // hash table of uint32 -> list of props (buckets +4/+8, count +0x10); saved
constexpr std::size_t kList5C = 0xA5C;        // list of props with property 0x0A751675, each referenced; saved
constexpr std::size_t kList60 = 0xA60;        // list of props with property 0x4A149631, each referenced; saved
constexpr std::size_t kList64 = 0xA64;        // list of props with property 0x6A959E71, each referenced; saved
constexpr std::size_t kSignsVisible = 0xA68;  // uint8 (1 at first); saved from version 5
constexpr std::size_t kSigns = 0xA6C;         // list of occupants of types 0xAB72FBB3 and 0xCB79919B (not referenced); saved from version 5
constexpr std::size_t kSize = 0xA70;
} // namespace field

// Logs a part's first call once ("prop: first <name>"; defined in
// prop_object.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::prop_sim
