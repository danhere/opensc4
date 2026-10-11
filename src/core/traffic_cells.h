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
// keys bit for bit. Route bytes give a big city's cells two bytes a
// coordinate. Ferries are not covered yet (docs/city-size.md, "Traffic cell
// keys").
#pragma once

#include "core/game_access.h"

#include <cstdint>
#include <cstring>
#include <map>

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

// A route's cell (route bytes: a head byte, then the cell): x and z one byte
// each as the game's; a big city's two each, low byte first.
inline const uint8_t* ReadRouteCell(const uint8_t* at, int32_t& x, int32_t& z)
{
    if (!g_wideCells) {
        x = at[0];
        z = at[1];
        return at + 2;
    }
    x = at[0] | at[1] << 8;
    z = at[2] | at[3] << 8;
    return at + 4;
}

template <typename Push>
void PushRouteCell(Push&& push, int32_t x, int32_t z)
{
    push(static_cast<uint8_t>(x));
    if (g_wideCells)
        push(static_cast<uint8_t>(static_cast<uint32_t>(x) >> 8));
    push(static_cast<uint8_t>(z));
    if (g_wideCells)
        push(static_cast<uint8_t>(static_cast<uint32_t>(z) >> 8));
}

// The link table (+0xC8, buildings linked to one another): the game's is a
// hash of uint16 cell keys, its nodes next, the key at +4 and the 0x2C-byte
// link at +8 (the linked cell as bytes at +8/+9, travel flags +0xA, nine
// costs +0xC, type +0x30). A big city's are kept here by CellKey instead, in
// the same layout with the linked cell whole after it; the game's table stays
// empty.
struct WideLink {
    char node[0x34];
    int32_t x;
    int32_t z;
};

inline std::map<uint32_t, WideLink>& WideLinks()
{
    static std::map<uint32_t, WideLink> links;
    return links;
}

// The link node of a cell, or null.
inline char* FindLink(void* simulator, int32_t x, int32_t z)
{
    if (g_wideCells) {
        auto& links = WideLinks();
        const auto it = links.find(CellKey(x, z));
        return it == links.end() ? nullptr : it->second.node;
    }
    const auto key = static_cast<uint16_t>(((static_cast<uint32_t>(x) & 0xFF) << 8) | static_cast<uint32_t>(z));
    char** const buckets = At<char**>(simulator, 0xC8 + 4);
    const auto count = static_cast<uint32_t>(At<char**>(simulator, 0xC8 + 8) - buckets);
    for (char* node = buckets[key % count]; node; node = At<char*>(node, 0))
        if (At<uint16_t>(node, 4) == key)
            return node;
    return nullptr;
}

// The linked cell of a link node.
inline int32_t LinkedX(const char* node)
{
    return g_wideCells ? reinterpret_cast<const WideLink*>(node)->x : static_cast<uint8_t>(node[8]);
}

inline int32_t LinkedZ(const char* node)
{
    return g_wideCells ? reinterpret_cast<const WideLink*>(node)->z : static_cast<uint8_t>(node[9]);
}

inline char* CellRecord(void* simulator, int32_t x, int32_t z)
{
    const int32_t index = g_recordStride == 256 ? ((x << 8) | z) : x * g_recordStride + z;
    return At<char*>(simulator, 0x58) + 0x2E * index;
}

} // namespace opensc4::traffic_sim
