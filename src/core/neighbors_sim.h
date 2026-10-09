// Shared between the parts of our cSC4NeighborsSimulator (build 641;
// docs/neighbors.md): the object's fields and the game's list helpers they
// use.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::neighbors_sim {

// Fields of cSC4NeighborsSimulator (offsets from the object's start; 0x40 bytes).
namespace field {
constexpr std::size_t kLoaded = 0x0C;      // uint8
constexpr std::size_t kInitialized = 0x0D; // uint8
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kCity = 0x14;
constexpr std::size_t kMessages = 0x18;    // the message server
constexpr std::size_t kConnections = 0x1C; // SC4List of connections (nodes from 0x00B207E0)
constexpr std::size_t kDeals = 0x20;       // SC4List of deals (nodes from 0x00B20D58)
constexpr std::size_t kPairs = 0x24;       // a list of pairs
constexpr std::size_t kTuning = 0x28;      // the tuning exemplar
constexpr std::size_t kCurve = 0x2C;       // an SC4Curve (16 bytes)
constexpr std::size_t kDealsDirty = 0x3C;  // uint8
constexpr std::size_t kUIDisabled = 0x3E;  // uint8
} // namespace field

// A node of the game's lists: next, previous, value.
struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};

inline ListNode* Head(void* self, std::size_t list) { return At<ListNode*>(self, list); }

// A node from the allocator cached at `allocator`, with `value` referenced,
// linked before `head` (at the end), as the game's inline push_back.
inline void PushBackReferenced(ListNode* head, void* value, std::uintptr_t allocator)
{
    auto* const node = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(allocator), 0xC, 0xCu));
    node->value = value;
    if (value)
        Virtual<uint32_t>(value, 0x4);
    ListNode* const last = head->prev;
    node->next = head;
    node->prev = last;
    last->next = node;
    head->prev = node;
}

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::neighbors_sim
