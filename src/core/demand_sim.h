// Shared between the parts of our cSC4DemandSimulator and cSC4Demand (build
// 641; docs/demand.md): the objects' fields, the game's map layout and the
// game's helpers they call.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::demand_sim {

// Fields of cSC4DemandSimulator (offsets from the object's start; 0xDC bytes).
namespace field {
constexpr std::size_t kLoaded = 0x0C;        // uint8: set by Read
constexpr std::size_t kInitialized = 0x0D;   // uint8
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kState = 0x14;         // the step's state (0 to 3)
constexpr std::size_t kCellIterator = 0x18;  // the cell walk of state 1 (0x20 bytes)
constexpr std::size_t kPending = 0x1C;       // map<uint32, float>: values counted by the cell walk
constexpr std::size_t kCounted = 0x28;       // map<uint32, float>: the last walk's values (saved)
constexpr std::size_t kCity = 0x34;
constexpr std::size_t kMessageServer = 0x38;
constexpr std::size_t kService3C = 0x3C;     // the service at 0x00B43D68
constexpr std::size_t kService40 = 0x40;     // the service at 0x00B43D08
constexpr std::size_t kService44 = 0x44;     // the service at 0x00B43D8C
constexpr std::size_t kOrdinances = 0x48;    // the service at 0x00B43D90
constexpr std::size_t kService4C = 0x4C;     // the service at 0x00B43DA4
constexpr std::size_t kDemands = 0x50;       // map<uint32, cISC4Demand*>
constexpr std::size_t kEffects = 0x5C;       // multimap<cISC4Demand*, effect>: demands that drive others
constexpr std::size_t kWeights = 0x68;       // uint32[12], tuning 0x8A63F2A1
constexpr std::size_t kSaved = 0xA0;         // four floats, saved: +0xA0, +0xA4, +0xA8, +0xAC
constexpr std::size_t kTaxCurve = 0xB0;      // SC4Curve*, tuning 0x4A1F38B5
} // namespace field

// cSC4Demand (0x54 bytes). Offsets from the object's start.
namespace demand {
constexpr std::size_t kLoaded = 0x08;        // uint8: set by Read
constexpr std::size_t kInitialized = 0x09;   // uint8
constexpr std::size_t kRefCount = 0x0C;
constexpr std::size_t kId = 0x10;
constexpr std::size_t kSupply = 0x14;        // float
constexpr std::size_t kDemand = 0x18;        // float
constexpr std::size_t kNewSupply = 0x1C;     // float: added this cycle
constexpr std::size_t kNewDemand = 0x20;     // float: added this cycle
constexpr std::size_t kSaved24 = 0x24;       // saved, otherwise unused
constexpr std::size_t kSaved28 = 0x28;       // saved, otherwise unused
constexpr std::size_t kMax = 0x2C;           // float: active demand maximum
constexpr std::size_t kMin = 0x30;           // float: active demand minimum
constexpr std::size_t kActive = 0x34;        // float: the active demand
constexpr std::size_t kFlag38 = 0x38;        // uint8, saved
constexpr std::size_t kEconomy = 0x3C;       // float: economy modifier
constexpr std::size_t kTax = 0x40;           // float: tax modifier
constexpr std::size_t kCap = 0x44;           // float (SC4Percentage): demand cap
constexpr std::size_t kRegionUse = 0x48;
constexpr std::size_t kCity = 0x4C;
constexpr std::size_t kMessageServer = 0x50;
} // namespace demand

// A node of the game's std::map (STLport red-black tree): color, parent,
// left, right, then the value. The header's parent is the root, its left the
// first node, its right the last.
struct TreeNode {
    uint32_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    uint32_t key;
};

template <typename T>
inline T& Value(TreeNode* node)
{
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(node) + 0x14);
}

inline TreeNode* Header(void* self, std::size_t offset) { return At<TreeNode*>(self, offset); }
inline uint32_t Size(void* self, std::size_t offset) { return At<uint32_t>(self, offset + 4); }
inline TreeNode* First(TreeNode* header) { return header->left; }

// In-order successor, as the game's inline code steps.
inline TreeNode* Next(TreeNode* node)
{
    if (TreeNode* right = node->right) {
        while (right->left)
            right = right->left;
        return right;
    }
    TreeNode* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return node->right != parent ? parent : node;
}

// std::map find: the node, or the header for none.
inline TreeNode* Find(TreeNode* header, uint32_t key)
{
    TreeNode* best = header;
    for (TreeNode* node = header->parent; node;) {
        if (node->key < key) {
            node = node->right;
        } else {
            best = node;
            node = node->left;
        }
    }
    return best != header && !(key < best->key) ? best : header;
}

using MapAtFn = uint32_t*(__thiscall*)(void* map, const uint32_t* key);
using FloatMapAtFn = float*(__thiscall*)(void* map, const uint32_t* key);
using TreeEraseFn = void(__thiscall*)(void* map, TreeNode* root);
using OperatorNewFn = void*(__cdecl*)(std::size_t size);
using MessageFn = void*(__thiscall*)(void* memory);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded

// std::map<uint32, uint32 or a pointer>::operator[] (0x00497A00).
inline uint32_t* MapAt(void* map, uint32_t key)
{
    return reinterpret_cast<MapAtFn>(addr::SC4Uint32Map_At)(map, &key);
}

// std::map<uint32, float>::operator[] (0x006A3170).
inline float* FloatMapAt(void* map, uint32_t key)
{
    return reinterpret_cast<FloatMapAtFn>(addr::SC4FloatMap_At)(map, &key);
}

inline void* DemandAt(void* self, uint32_t id)
{
    return *reinterpret_cast<void**>(MapAt(static_cast<char*>(self) + field::kDemands, id));
}

inline const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);

// __ftol2: truncation toward zero, the low 32 bits.
inline int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// FILD of a uint32 (with the 2^32 correction for "negative" words).
inline double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

// A new cGZMessage (RZ operator new, its constructor), referenced.
inline void* NewMessage()
{
    void* const memory = reinterpret_cast<OperatorNewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    return message;
}

// Calls a game function by address (thiscall); the game's or ours, as the
// replacements are configured.
template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::demand_sim
