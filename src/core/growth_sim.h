// Shared between the parts of our cSC4GrowthDeveloper (build 641;
// docs/growth.md): the object's fields as far as they are known, and the
// helpers the parts share. A growth developer keeps the maps one developer
// type (residential, commercial or industrial by wealth) grows from:
// desirability, capacity, population and maximum capacity, and the growth
// and decline lists the building development simulator reads.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::growth_sim {

// Fields of cSC4GrowthDeveloper (offsets from the object's start; 0x13C
// bytes; constructor 0x006B7310). Names are ours. The interfaces: +0x0 the
// primary cISC4GrowthDeveloper (0x00AB1488, 32 slots), +0x4
// cIGZMessageTarget2 (0x00AB1474, DoMessage 0x006B7030), +0x8
// cIGZSerializable (0x00AB145C: Write 0x006B3960, Read 0x006B39F0,
// GetGZCLSID 0x006B3790).
namespace field {
constexpr std::size_t kInitialized = 0xC;     // uint8
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kCity = 0x14;           // 0x00B43CEC
constexpr std::size_t kMessages = 0x18;       // the message server (0x00B43CCC)
constexpr std::size_t kDevelopment = 0x1C;    // the building development simulator (0x00B43D68)
constexpr std::size_t kService20 = 0x20;      // 0x00B43D88
constexpr std::size_t kCityService24 = 0x24;  // the city's slot 34 (+0x88)
constexpr std::size_t kService28 = 0x28;      // 0x00B43DA8
constexpr std::size_t kZones = 0x2C;          // 0x00B43D14
constexpr std::size_t kProperties = 0x30;     // the property holder Init was given
constexpr std::size_t kDemand34 = 0x34;       // the source demand (service 0x00B43D74 slot 6, property 0x87CD6301; shared slot 18)
constexpr std::size_t kPurpose = 0x38;        // property 0x87CD6332 widened to uint32 (shared slot 7, PurposeType in gzcom-dll); compared with the development simulator slot 39 in 0x006B51C0
constexpr std::size_t kWealth = 0x3C;         // wealth (property 0x87CD6330; shared slot 8); DoMessage compares message data 4 with it
constexpr std::size_t kTractSize = 0x44;      // the grids' tract size (64 at first; property 0x87CD6310)
constexpr std::size_t kDemandSent = 0x104;    // int32: the total last sent to the demand +0x34; saved
constexpr std::size_t kTotalCapacity = 0x108; // uint32 (shared slot 17); 0x006B51C0 adds to it; saved
constexpr std::size_t kGrid10C = 0x10C;       // Uint8 grid (constructor 0x0045DDB0), ID +0xCC + 0x1000000; saved
constexpr std::size_t kGrid110 = 0x110;       // Sint16 grid, ID + 0x2100000; not saved
constexpr std::size_t kGrid114 = 0x114;       // Sint16 grid (shared slot 13: desirability), ID + 0x2000000; saved
constexpr std::size_t kGrid118 = 0x118;       // Sint16 grid (shared slot 15: population), ID + 0x3000000; saved
constexpr std::size_t kGrid11C = 0x11C;       // Uint16 grid (shared slot 14: capacity), ID + 0x5000000; saved
constexpr std::size_t kGrid120 = 0x120;       // Uint16 grid (shared slot 16: maximum capacity), ID + 0x6000000; saved
constexpr std::size_t kSubType = 0xCC;        // property 0x21 of the holder; the grids' IDs add it
constexpr std::size_t kZoneKind = 0xD0;       // TargetZoneType 1-3 (property 0x87CD6300; shared slot 5)
constexpr std::size_t kMapState = 0xD4;       // the map update's state: 0 idle, 1 started, below 0x11 the lists are valid
} // namespace field

// Logs a part's first call once (defined in growth_object.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::growth_sim
