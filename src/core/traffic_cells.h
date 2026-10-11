// Shared between the parts of our cSC4TrafficSimulator (build 641;
// docs/traffic.md): where a cell's 46-byte record (+0x58) sits. A record
// holds uint16 ways by travel type (bit to + 4 x from), uint16 change
// nibbles by type at +0x12, side words at +0x24 and flags at +0x2C.
//
// The game's table is 65,536 records indexed x << 8 | z, so it holds a city
// of at most 256 cells a side. With big cities (docs/city-size.md, "The 8 km
// spike", failure 2) ours is sized from the city instead, rows of the city's
// z cells; standard sizes keep the game's layout and index.
//
// The same goes for the keys that pack a cell: the game's are 8 bits a
// coordinate (x << 8 | z), and 32-bit keys put a travel type above them at
// bit 16 and flags in the top byte. For a city past 256 cells a side, ours
// take 10 bits a coordinate (cities up to 1024 cells, 16 km) and the type at
// bit 20; the top byte stays where it was. Standard sizes keep the game's
// keys bit for bit. Route bytes and the 16-bit cell tables are not covered
// yet (docs/city-size.md, "Traffic cell keys").
#pragma once

#include "core/game_access.h"

#include <cstdint>

namespace opensc4::traffic_sim {

// Records from one x to the next: 256, or the city's z cells when Init
// sized the table for a big city.
inline int32_t g_recordStride = 256;

// Init sets it when the city is past 256 cells a side (big cities only).
inline bool g_wideCells = false;

inline uint32_t CellBits()
{
    return g_wideCells ? 10u : 8u;
}

// x and z packed in the low bits of a key.
inline uint32_t CellKey(int32_t x, int32_t z)
{
    const uint32_t bits = CellBits();
    const uint32_t mask = (1u << bits) - 1;
    return (static_cast<uint32_t>(x) & mask) << bits | (static_cast<uint32_t>(z) & mask);
}

inline int32_t KeyX(uint32_t key)
{
    const uint32_t bits = CellBits();
    return static_cast<int32_t>((key >> bits) & ((1u << bits) - 1));
}

inline int32_t KeyZ(uint32_t key)
{
    return static_cast<int32_t>(key & ((1u << CellBits()) - 1));
}

// A 32-bit key: `high` in the top byte, a travel type, then the cell (the
// edge density keys: evening << 24 | type << 16 | x << 8 | z; the path
// finder's nodes: sides and flags << 24 | type << 16 | x << 8 | z).
inline uint32_t TypedCellKey(uint32_t high, uint32_t type, int32_t x, int32_t z)
{
    return high << 24 | type << (2 * CellBits()) | CellKey(x, z);
}

inline uint32_t KeyType(uint32_t key)
{
    return g_wideCells ? (key >> 20) & 0xF : (key >> 16) & 0xFF;
}

inline char* CellRecord(void* simulator, int32_t x, int32_t z)
{
    const int32_t index = g_recordStride == 256 ? ((x << 8) | z) : x * g_recordStride + z;
    return At<char*>(simulator, 0x58) + 0x2E * index;
}

} // namespace opensc4::traffic_sim
