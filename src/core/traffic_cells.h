// Shared between the parts of our cSC4TrafficSimulator (build 641;
// docs/traffic.md): where a cell's 46-byte record (+0x58) sits. A record
// holds uint16 ways by travel type (bit to + 4 x from), uint16 change
// nibbles by type at +0x12, side words at +0x24 and flags at +0x2C.
//
// The game's table is 65,536 records indexed x << 8 | z, so it holds a city
// of at most 256 cells a side. With big cities (docs/city-size.md, "The 8 km
// spike", failure 2) ours is sized from the city instead, rows of the city's
// z cells; standard sizes keep the game's layout and index.
#pragma once

#include "core/game_access.h"

#include <cstdint>

namespace opensc4::traffic_sim {

// Records from one x to the next: 256, or the city's z cells when Init
// sized the table for a big city.
inline int32_t g_recordStride = 256;

inline char* CellRecord(void* simulator, int32_t x, int32_t z)
{
    const int32_t index = g_recordStride == 256 ? ((x << 8) | z) : x * g_recordStride + z;
    return At<char*>(simulator, 0x58) + 0x2E * index;
}

} // namespace opensc4::traffic_sim
