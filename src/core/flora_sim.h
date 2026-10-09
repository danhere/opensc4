// Shared between the parts of our cSC4FloraSimulator (build 641;
// docs/flora.md): the object's fields, the global flora data, and the
// game's container helpers they use.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::flora_sim {

// Fields of cSC4FloraSimulator (offsets from the object's start; 0xB8 bytes).
namespace field {
constexpr std::size_t kInitialized = 0x0C;   // uint8
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kOccupants = 0x14;     // the occupant manager
constexpr std::size_t kCellService = 0x18;   // the service at 0x00B43DAC: a location's column (slot 19)
constexpr std::size_t kSimulator = 0x1C;     // the simulator service
constexpr std::size_t kTerrain = 0x20;
constexpr std::size_t kLots = 0x24;          // the service at 0x00B43D08
constexpr std::size_t kFilter = 0x28;        // an occupant filter (vtables 0x00AB1374, 0x00AB135C)
constexpr std::size_t kEnabled = 0x2C;       // uint8
constexpr std::size_t kSeeding = 0x2D;       // uint8: trees spread
constexpr std::size_t kSizeX = 0x30;         // float, meters
constexpr std::size_t kSizeZ = 0x34;
constexpr std::size_t kCellsX = 0x38;
constexpr std::size_t kCellsZ = 0x3C;
constexpr std::size_t kDate = 0x40;          // the date of the month's pass
constexpr std::size_t kTractsPerMonth = 0x48;
constexpr std::size_t kSeedsPerMonth = 0x4C;
constexpr std::size_t kLoaded = 0x50;        // uint8
constexpr std::size_t kWalk = 0x54;          // the tract walk's rectangle iterator
constexpr std::size_t kRandom = 0x88;        // the simulator's cRZRandom
constexpr std::size_t kSlopeScale = 0xA0;    // float
constexpr std::size_t kDying = 0xA4;         // list of dying flora occupants
constexpr std::size_t kDeathTimer = 0xA8;    // float
constexpr std::size_t kDeaths = 0xAC;        // float
constexpr std::size_t kDeathRate = 0xB0;     // float
constexpr std::size_t kDeathsPerStep = 0xB4; // float
} // namespace field

// A node of the game's maps (std::map<uint32_t, T*>): +4 parent, +8
// left, +0xC right, +0x10 key, +0x14 value. The map is a pointer to its
// head node (whose +4 is the root, +8 the leftmost, +0xC the rightmost).
struct MapNode {
    uint32_t color;
    MapNode* parent;
    MapNode* left;
    MapNode* right;
    uint32_t key;
    char* value;
};

// The flora types (0x00B49050) and families (0x00B4905C), and the types in
// load order (0x00B49044).
inline MapNode*& TypeMapHead() { return Global<MapNode*>(addr::cSC4FloraSimulator_TypeMap); }
inline MapNode*& FamilyMapHead() { return Global<MapNode*>(addr::cSC4FloraSimulator_FamilyMap); }

// In-order successor, as the game's inline code.
inline MapNode* Next(MapNode* node)
{
    if (MapNode* right = node->right) {
        while (right->left)
            right = right->left;
        return right;
    }
    MapNode* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return node->right != parent ? parent : node;
}

inline MapNode* Find(std::uintptr_t map, uint32_t key)
{
    using FindFn = MapNode**(__thiscall*)(std::uintptr_t map, MapNode** out, const uint32_t* key);
    MapNode* found = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(map, &found, &key);
    return found;
}

inline char* FloraData(uint32_t type)
{
    MapNode* const node = Find(addr::cSC4FloraSimulator_TypeMap, type);
    return node == TypeMapHead() ? nullptr : node->value;
}

void FirstCall(const char* name, bool& logged);

// A new type record as the game allocates it (0x178 bytes, one reference,
// the defaults), and dropping a reference (0x00472C70) (flora_types.cpp).
char* NewData();
void ReleaseData(char* data);

// DoMessage's cheat code part (message 0x230E27AC): false when the code is
// not "Flora" with a word, and DoMessage goes on (flora_object.cpp).
bool CheatMessage(void* self, void* message);

} // namespace opensc4::flora_sim
