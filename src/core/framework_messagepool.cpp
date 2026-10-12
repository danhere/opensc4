// The standard message's beginning and end, and the pool its memory comes
// from, on the game's own objects (docs/framework.md). Replacements, each
// off by default:
//
//   framework.message.lifetime  the constructor (0x009134D6) and the
//                               destructor (0x009133AB)
//   framework.message.pool      the pool's allocate (0x009133DA) and free
//                               (0x00913409)
//
// The pool (0x00B627E0) has nine block allocators at +0x6C, for blocks of
// 8, 16, 32 and so on to 2,048 bytes. A request gets four more bytes in
// front for a header, and goes to the first allocator whose block holds
// it; the header is the allocator's number in the top byte and the size
// (with the header) below. What no allocator holds comes from the C
// runtime, with the top bit set in the header. Both functions hold the
// pool's lock (0x00B62870) around this.
//
// Still the game's: the block allocators themselves (their slot 0 gives a
// block, slot 1 takes one back), which other pools share, and the C
// runtime.
#include "core/address_check.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kRefs = 0x04;
constexpr std::size_t kFlags = 0x08;
constexpr std::size_t kType = 0x0C;
constexpr std::size_t kData = 0x10;
constexpr std::size_t kString = 0x20;
constexpr std::size_t kUnknown = 0x24;
constexpr std::size_t kExtra = 0x28;

constexpr std::size_t kAllocators = 0x6C;
constexpr uint32_t kAllocatorCount = 9;
constexpr uint32_t kFromRuntime = 0x80000000;

// The game's 0x009134D6: an empty message without a reference.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cGZMessage_Vtable;
    At<uint32_t>(self, kRefs) = 0;
    At<uint8_t>(self, kFlags) = 0;
    At<uint32_t>(self, kType) = 0;
    for (unsigned i = 0; i < 4; ++i)
        At<uint32_t>(self, kData + 4 * i) = 0;
    At<void*>(self, kString) = nullptr;
    At<void*>(self, kUnknown) = nullptr;
    At<void*>(self, kExtra) = nullptr;
    return self;
}

// The game's constructor returns the message in EAX, leaves ECX at 0 and
// keeps every other register; its 282 callers may count on any of that.
__declspec(naked) void ConstructThunk()
{
    __asm push edx
    __asm call Construct
    __asm pop edx
    __asm xor ecx, ecx
    __asm ret
}

// The game's 0x009133AB: the vtable set back to the standard message's (a
// subclass's destructor ends here), then the extra object, the unknown and
// the string released, in that order.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cGZMessage_Vtable;
    Release(At<void*>(self, kExtra));
    Release(At<void*>(self, kUnknown));
    Release(At<void*>(self, kString));
}

// The game's 0x009133DA.
void* __cdecl PoolAllocate(uint32_t size)
{
    Lock lock(reinterpret_cast<void*>(addr::RZ_PoolLock));
    char* const pool = reinterpret_cast<char*>(addr::RZ_Pool);
    const uint32_t needed = size + 4;
    uint32_t index = 0;
    for (uint32_t block = 8; needed > block; block += block)
        if (++index == kAllocatorCount)
            break;
    uint32_t* header;
    if (index == kAllocatorCount) {
        header = static_cast<uint32_t*>(reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::CRT_malloc)(needed));
        *header = needed | kFromRuntime;
    } else {
        header = Virtual<uint32_t*>(At<void*>(pool, kAllocators + 4 * index), 0x0);
        *header = (index << 24) | needed;
    }
    return header + 1;
}

// The game's 0x00913409: nothing for a null pointer; the header's top byte
// says where the block goes back.
void __cdecl PoolFree(void* memory)
{
    if (!memory)
        return;
    Lock lock(reinterpret_cast<void*>(addr::RZ_PoolLock));
    char* const pool = reinterpret_cast<char*>(addr::RZ_Pool);
    void* const block = static_cast<char*>(memory) - 4;
    const uint32_t index = static_cast<uint8_t*>(memory)[-1];
    if (index < kAllocatorCount)
        Virtual<void>(At<void*>(pool, kAllocators + 4 * index), 0x4, block);
    else
        reinterpret_cast<void(__cdecl*)(void*)>(addr::CRT_free)(block);
}

OPENSC4_THUNK(DestructThunk, Destruct, 0)

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    return true;
}

bool InstallLifetime(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZMessage_cGZMessage, reinterpret_cast<void*>(&ConstructThunk)},
        {addr::cGZMessage_Destruct, reinterpret_cast<void*>(&DestructThunk)},
    };
    return InstallAll(detours);
}

bool InstallPool(registry::Mode)
{
    const Detour detours[] = {
        {addr::RZ_operator_new, reinterpret_cast<void*>(&PoolAllocate)},
        {addr::RZ_operator_delete, reinterpret_cast<void*>(&PoolFree)},
    };
    return InstallAll(detours);
}

const bool g_registeredLifetime = registry::Register({
    "framework.message.lifetime",
    "framework/message",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZMessage_cGZMessage,
    "Our standard message: its constructor and destructor (docs/framework.md)",
    false,
    InstallLifetime,
});

const bool g_registeredPool = registry::Register({
    "framework.message.pool",
    "framework/message",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::RZ_operator_new,
    "Our message pool: allocate and free (docs/framework.md)",
    false,
    InstallPool,
});

} // namespace
} // namespace opensc4::framework
