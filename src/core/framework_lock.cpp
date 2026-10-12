// The critical section wrapper (vtable 0x00AD1EF4: the lock object the
// framework, the message servers, the pools and many game classes hold) on
// the game's own objects (docs/framework.md). Replacements, each off by
// default:
//
//   framework.lock.use       Lock (0x00930B35), Unlock (0x00930B6D), TryLock
//                            (0x00930B48), GetLockCount (0x00930B2E),
//                            GetHandle (0x00930B82)
//   framework.lock.lifetime  the constructor (0x00930B91), the destructor
//                            (0x00930B02), Release (0x00930B20), the
//                            deleting destructor (0x00930BC3) and the
//                            lookup of TryEnterCriticalSection (0x00930AC0)
//
// Locks are made from the program's first moments (static objects), and the
// lookup runs once among them, so the lifetime group goes in when core.dll
// is injected (phase Attach).
//
// The object is 8 bytes: +0 vtable, +4 a block of 0x1C bytes from the C
// runtime: a CRITICAL_SECTION (0x18 bytes) and at +0x18 the wrapper's own
// count of locks held.
//
// Windows is called through the exe's own import slots, as the game's code
// does.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kSection = 0x04;
constexpr std::size_t kCount = 0x18;
constexpr uint32_t kBlockSize = 0x1C;

using SectionFn = void(__stdcall*)(void* section);
using TryFn = int(__stdcall*)(void* section);

void Enter(char* section)
{
    Global<SectionFn>(addr::Kernel32_EnterCriticalSection)(section);
}

// The game's 0x00930B35.
uint32_t __fastcall LockSection(char* self, void*)
{
    char* const section = At<char*>(self, kSection);
    Enter(section);
    return ++At<uint32_t>(section, kCount);
}

// The game's 0x00930B6D: the count goes down while the section is still
// held.
uint32_t __fastcall UnlockSection(char* self, void*)
{
    char* const section = At<char*>(self, kSection);
    const uint32_t count = --At<uint32_t>(section, kCount);
    Global<SectionFn>(addr::Kernel32_LeaveCriticalSection)(section);
    return count;
}

// The game's 0x00930B48: 0 when the try fails. When it succeeds the game
// enters the section again, so Windows counts two entries for one lock and
// one Unlock does not give the section up; kept as it is. Without the
// function (Windows 95) this is Lock.
uint32_t __fastcall TryLockSection(char* self, void*)
{
    char* const section = At<char*>(self, kSection);
    const TryFn tryEnter = Global<TryFn>(addr::RZCriticalSection_TryEnterFunction);
    if (tryEnter && !tryEnter(section))
        return 0;
    Enter(section);
    return ++At<uint32_t>(section, kCount);
}

// The game's 0x00930B2E.
uint32_t __fastcall GetLockCount(char* self, void*)
{
    return At<uint32_t>(At<char*>(self, kSection), kCount);
}

// The game's 0x00930B82.
uint32_t __fastcall GetHandle(char* self, void*, void** out)
{
    *out = At<void*>(self, kSection);
    return 1;
}

// The game's 0x00930B91: the block's count and section cleared (0x00930AA8,
// when the C runtime gave a block), then InitializeCriticalSection.
char* __fastcall Construct(char* self, void*)
{
    At<char*>(self, kSection) = nullptr;
    At<std::uintptr_t>(self, 0) = addr::RZCriticalSection_Vtable;
    char* const block = reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::CRT_malloc)(kBlockSize);
    if (block) {
        At<uint32_t>(block, kCount) = 0;
        reinterpret_cast<void*(__cdecl*)(void*, int, uint32_t)>(addr::CRT_memset)(block, 0, kCount);
    }
    At<char*>(self, kSection) = block;
    Global<SectionFn>(addr::Kernel32_InitializeCriticalSection)(block);
    return self;
}

// The game's 0x00930B02.
void __fastcall Destroy(char* self, void*)
{
    char* const section = At<char*>(self, kSection);
    At<std::uintptr_t>(self, 0) = addr::RZCriticalSection_Vtable;
    Global<SectionFn>(addr::Kernel32_DeleteCriticalSection)(section);
    if (section)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::CRT_free)(section);
}

// The game's 0x00930B20.
uint32_t __fastcall ReleaseSection(char* self, void*)
{
    if (self)
        Virtual<void*>(self, 0x18, uint32_t{1});
    return 0;
}

// The game's 0x00930BC3.
char* __fastcall DeletingDestructor(char* self, void*, uint8_t flags)
{
    reinterpret_cast<void(__thiscall*)(char*)>(addr::RZCriticalSection_Destroy)(self);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// The game's 0x00930AC0, a static object's constructor: everything but
// Windows 95 (version 4.0 of the old line) is asked for
// TryEnterCriticalSection.
char* __fastcall FindTryEnter(char* self, void*)
{
    using VersionFn = uint32_t(__stdcall*)();
    using LoadFn = void*(__stdcall*)(const char* name);
    using FindFn = void*(__stdcall*)(void* module, const char* name);
    const uint32_t version = Global<VersionFn>(addr::Kernel32_GetVersion)();
    const bool windows95 = version >= 0x80000000 && (version & 0xFF) <= 4 && ((version >> 8) & 0xFF) < 1;
    if (!windows95) {
        void* const module = Global<LoadFn>(addr::Kernel32_LoadLibraryA)("Kernel32.dll");
        Global<void*>(addr::RZCriticalSection_Kernel32Module) = module;
        if (module)
            Global<void*>(addr::RZCriticalSection_TryEnterFunction) =
                Global<FindFn>(addr::Kernel32_GetProcAddress)(module, "TryEnterCriticalSection");
    }
    log::Write("framework: lock: the lookup of TryEnterCriticalSection ran in our code");
    return self;
}

OPENSC4_THUNK(GetLockCountThunk, GetLockCount, 0)

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

bool InstallUse(registry::Mode)
{
    const Detour detours[] = {
        {addr::RZCriticalSection_Lock, reinterpret_cast<void*>(&LockSection)},
        {addr::RZCriticalSection_Unlock, reinterpret_cast<void*>(&UnlockSection)},
        {addr::RZCriticalSection_TryLock, reinterpret_cast<void*>(&TryLockSection)},
        {addr::RZCriticalSection_GetLockCount, reinterpret_cast<void*>(&GetLockCountThunk)},
        {addr::RZCriticalSection_GetHandle, reinterpret_cast<void*>(&GetHandle)},
    };
    return InstallAll(detours);
}

bool InstallLifetime(registry::Mode)
{
    const Detour detours[] = {
        {addr::RZCriticalSection_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::RZCriticalSection_Destroy, reinterpret_cast<void*>(&Destroy)},
        {addr::RZCriticalSection_Release, reinterpret_cast<void*>(&ReleaseSection)},
        {addr::RZCriticalSection_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::RZCriticalSection_FindTryEnter, reinterpret_cast<void*>(&FindTryEnter)},
    };
    return InstallAll(detours);
}

const bool g_registeredUse = registry::Register({
    "framework.lock.use",
    "framework/lock",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::RZCriticalSection_Lock,
    "Our critical section wrapper: Lock, Unlock, TryLock and two getters (docs/framework.md)",
    false,
    InstallUse,
});

const bool g_registeredLifetime = registry::Register({
    "framework.lock.lifetime",
    "framework/lock",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::RZCriticalSection_Construct,
    "Our critical section wrapper: constructor, destructor, Release and the lookup of "
    "TryEnterCriticalSection (docs/framework.md)",
    false,
    InstallLifetime,
});

} // namespace
} // namespace opensc4::framework
