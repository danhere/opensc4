// The list helpers the pollution simulator's recycling pass uses (docs/
// pollution.md): assignment of one list of object pointers to another for
// the node allocator at 0x00B207E4, and the two range helpers it calls. The
// residential simulator calls the same three. Replacement, off by default:
//
//   simulators.pollution.lists: Assign (0x006E5660), EraseRange (0x006E41F0)
//                               and InsertRange (0x006E4270).
//
// A list is a pointer to its sentinel node; a node is {next, previous,
// value}.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::pollution_sim {
namespace {

constexpr std::size_t kNext = 0x0;
constexpr std::size_t kPrevious = 0x4;
constexpr std::size_t kValue = 0x8;

// The game's 0x006E41F0: the nodes from `first` up to `last` unlinked and
// given back to the allocator; `last` through `out`.
char** __fastcall EraseRange(char*, void*, char** out, char* first, char* last)
{
    for (char* node = first; node != last;) {
        char* const gone = node;
        node = At<char*>(node, kNext);
        char* const previous = At<char*>(gone, kPrevious);
        char* const next = At<char*>(gone, kNext);
        At<char*>(previous, kNext) = next;
        At<char*>(next, kPrevious) = previous;
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator2), 0x10, gone);
    }
    *out = last;
    return out;
}

// The game's 0x006E4270: a new node before `position` for each node from
// `first` up to `last`, with its value. The fourth argument is not read.
void __fastcall InsertRange(char*, void*, char* position, char* first, char* last, void*)
{
    for (char* from = first; from != last; from = At<char*>(from, kNext)) {
        char* const node = Virtual<char*>(NodeAllocatorAt(addr::SC4List_NodeAllocator2), 0xC, uint32_t{0xC});
        At<void*>(node, kValue) = At<void*>(from, kValue);
        char* const previous = At<char*>(position, kPrevious);
        At<char*>(node, kNext) = position;
        At<char*>(node, kPrevious) = previous;
        At<char*>(previous, kNext) = node;
        At<char*>(position, kPrevious) = node;
    }
}

// The game's 0x006E5660: the other list's values copied into this list's
// nodes; then this list's remaining nodes erased, or the other's remaining
// values appended.
char* __fastcall Assign(char* self, void*, char* other)
{
    if (self == other)
        return self;
    char* const end = At<char*>(self, 0x0);
    char* to = At<char*>(end, kNext);
    char* const otherEnd = At<char*>(other, 0x0);
    char* from = At<char*>(otherEnd, kNext);
    while (to != end && from != otherEnd) {
        At<void*>(to, kValue) = At<void*>(from, kValue);
        from = At<char*>(from, kNext);
        to = At<char*>(to, kNext);
    }
    if (from == otherEnd) {
        char* result;
        EraseRange(self, nullptr, &result, to, end);
    } else {
        char tag;
        InsertRange(self, nullptr, end, from, otherEnd, &tag);
    }
    return self;
}

void* g_originalAssign = nullptr;
void* g_originalErase = nullptr;
void* g_originalInsert = nullptr;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const struct {
        std::uintptr_t address;
        void* ours;
        void** original;
    } hooks[] = {
        {addr::SC4List_Assign, reinterpret_cast<void*>(&Assign), &g_originalAssign},
        {addr::SC4PointerList_EraseRange, reinterpret_cast<void*>(&EraseRange), &g_originalErase},
        {addr::SC4PointerList_InsertRange, reinterpret_cast<void*>(&InsertRange), &g_originalInsert},
    };
    for (const auto& h : hooks) {
        const addr::Entry* entry = addr_check::Find(h.address);
        if (!entry || !hook::Install(*entry, h.ours, h.original))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "simulators.pollution.lists",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4List_Assign,
    "Our list assignment and its two range helpers, used by the recycling pass (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
