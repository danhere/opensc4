// Shared between the parts of our cSC4BuildingDevelopmentSimulator (build
// 641; docs/development.md): the object's fields as far as they are known,
// and the helpers the parts share. The building development simulator
// keeps the growth developers, the building and foundation types, the
// construction resources and the development cheats.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::development_sim {

// Fields of cSC4BuildingDevelopmentSimulator (offsets from the object's
// start; 0xE4 bytes; constructor 0x0069A000). Names are ours. The
// interfaces: +0x0 the primary (0x00AB0AF8), +0x4 cIGZMessageTarget (IID
// 0xE98B2F57, 0x00AB0AE8, an empty DoMessage), +0x8 cIGZMessageTarget2
// (0x00AB0AD8), +0xC cIGZSerializable (0x00AB0AC0).
namespace field {
constexpr std::size_t kLoaded = 0x10;         // uint8: Read ran (Init then skips resetting the flags +0x50)
constexpr std::size_t kInitialized = 0x11;    // uint8
constexpr std::size_t kRefCount = 0x14;
constexpr std::size_t kMessages = 0x18;       // the message server
constexpr std::size_t kCity = 0x1C;
constexpr std::size_t kOccupants = 0x20;      // the city's slot 34 (the occupant manager)
constexpr std::size_t kResources = 0x24;      // the resource manager (0x00B43CA8)
constexpr std::size_t kZones = 0x28;          // the zone manager (0x00B43D14)
constexpr std::size_t kLots = 0x2C;           // the lot manager (0x00B43D08)
constexpr std::size_t kService30 = 0x30;      // 0x00B43D88
constexpr std::size_t kService34 = 0x34;      // 0x00B43DA0
constexpr std::size_t kTerrain = 0x38;        // 0x00B43CF4
constexpr std::size_t kTerrain3C = 0x3C;      // int32: terrain slot 7 + 1
constexpr std::size_t kTerrain40 = 0x40;      // int32: terrain slot 7
constexpr std::size_t kTerrain44 = 0x44;      // int32: terrain slot 8
constexpr std::size_t kGrid = 0x48;           // a bit grid (0x49D5B678), size 1
constexpr std::size_t kList = 0x4C;           // list of occupants (IID 0x26BB767E), referenced
constexpr std::size_t kFlags = 0x50;          // map<int32 developer type, uint8> (head; size +0x54)
constexpr std::size_t kDevelopers = 0x5C;     // map<uint32 developer type, developer (IID 0xE7CDD662)> (head; size +0x60)
constexpr std::size_t kService68 = 0x68;      // 0x00B43D24
constexpr std::size_t kService6C = 0x6C;      // 0x00B43D20
constexpr std::size_t kDemand = 0x70;         // a cSC4Demand (IID 0x0797A4E8), Init 100, 0
constexpr std::size_t kList7C = 0x7C;         // list, 0x1C-byte nodes
constexpr std::size_t kList80 = 0x80;         // list, 0x1C-byte nodes
constexpr std::size_t kTree84 = 0x84;         // a tree (head; size +0x88)
constexpr std::size_t kTree90 = 0x90;         // a tree (head; size +0x94)
constexpr std::size_t kCells = 0x9C;          // hash table (developer, x, z, byte) -> uint32 (buckets +0xA0/+0xA4, count +0xAC)
constexpr std::size_t kTreeB0 = 0xB0;         // a tree (head; size +0xB4)
constexpr std::size_t kTuningC0 = 0xC0;       // uint32 pair (100, 2000), tuning 0x47E2C380
constexpr std::size_t kTuningC8 = 0xC8;       // float 0.5, tuning 0x47E2C381
constexpr std::size_t kTuningCC = 0xCC;       // uint32 10, tuning 0x47E2C382
constexpr std::size_t kTuningD0 = 0xD0;       // float vector, tuning 0x47E2C383
constexpr std::size_t kCheatDD = 0xDD;        // uint8, saved
constexpr std::size_t kCheatIgnorePower = 0xDE; // uint8, saved
constexpr std::size_t kCheatIgnoreCaps = 0xDF;  // uint8, saved
constexpr std::size_t kCheatCoffeeBar = 0xE1; // uint8
} // namespace field

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::development_sim
