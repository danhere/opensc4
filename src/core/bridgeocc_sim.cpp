// Our cSC4NetworkBridgeOccupant (build 641; the class map, the vtables and
// the fields are in src/core/bridgeocc_sim.h): the bridge's own functions,
// each a detour at the game's address, because the thunks of the other
// vtable parts jump to them and a maker function calls the constructor and
// the pool allocation. The parent class (the pre-built model network
// occupant, class ID 0x49C1A034) is not ours here (src/core/prebuiltocc_sim.cpp
// has it): its functions are called at their addresses, with the argument
// counts below. Slot 81 (0x00605070), which the model code calls with a model
// instance, is left to the game: drawing is not this work's.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither are GetGZCLSID 0x00604BB0 (only EAX; one direct caller, the class
// registration at 0x00460748, which loads ECX again after it) and the page
// set-up 0x00604C60 up to its call (it does not read ECX). `self` is the
// object's start (the interface part) unless a function says `part` (the
// occupant part's Init gets the object + 4).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the object's own slots: 0x4 AddRef none (0x0065A860); the occupant
//     part's 0x14 IsInitialized none (0x00458A40);
//   - the member at +0x1D0 (vtable 0x00AA4060): 0x4 none (0x005E86B0, ret),
//     0x8 one (0x005E5E60, ret 4), 0xC none (0x0060CE80, ret); its
//     constructor 0x005E8670 and destructor 0x00513E00 none;
//   - the parent class: constructor 0x0061E1A0 one (the flag, ret 4),
//     destructor 0x00604CA0 none (it ends in a jump to the network
//     occupant's 0x0061C170), QueryInterface 0x0061D060 two (ret 8), slot 4
//     0x0061D0D0 none, the occupant part's Init 0x0061DB00 none;
//   - the page allocator 0x0090D8E0 (cdecl, one), the pool's Grow 0x0090CF9A
//     (none), ReleaseBlock 0x0090D031 and ReopenBlock 0x0090CFF9 (one each,
//     ret 4).
#include "core/bridgeocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::bridgeocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("bridgeocc: first %s", name);
}

namespace {

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// ====================================================================================
// simulators.bridgeocc.slots: the class ID, QueryInterface, the occupant part's Init, Shutdown
// ====================================================================================

// The serializable part's slot 5 (0x00604BB0): the class ID. Only EAX is
// touched; the part is not read.
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// Slot 0 (0x00604BC0, ret 8; the other parts' thunks take off their offset
// and jump here): the bridge's IID (its class ID) gives the object itself,
// referenced through slot 1; every other ID goes to the parent class's
// QueryInterface by a tail jump. The game's answer in the first case is
// AL = 1 and the rest of EAX from AddRef.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidBridge) {
        *object = self;
        const uint32_t count = Virtual<uint32_t>(self, 0x4);
        return (count & 0xFFFFFF00u) | 1u;
    }
    return CallThis<uint32_t, uint32_t, void**>(addr::cSC4PrebuiltNetworkOccupant_QueryInterface, self, iid,
                                                object);
}

// The occupant part's slot 3 (0x00604BF0; `part` is the object + 4): an
// object that is not initialised yet goes to the parent class's occupant
// Init by a tail jump (its answer is the result); one that is answers
// AL = 1 (the rest of EAX is what IsInitialized left, here its whole
// answer's upper bytes).
uint32_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    const uint32_t initialised = Virtual<uint32_t>(part, occupant::kIsInitialized);
    if (!static_cast<uint8_t>(initialised))
        return CallThis<uint32_t>(addr::cSC4PrebuiltNetworkOccupant_OccupantInit, part);
    return (initialised & 0xFFFFFF00u) | 1u;
}

// Slot 4 (0x00604C10; the occupant part's slot 4 is a thunk to it): an
// object that is not initialised answers AL = 1 (the rest of EAX is
// IsInitialized's). Otherwise, when the member at +0x1D0 has an owner
// (its slot 3), the member is stopped (slot 1) and its owner set to none
// (slot 2 with 0); then the parent class's slot 4 by a tail jump (its
// answer is the result).
uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    const uint32_t initialised = Virtual<uint32_t>(self + field::kOccupant, occupant::kIsInitialized);
    if (!static_cast<uint8_t>(initialised))
        return (initialised & 0xFFFFFF00u) | 1u;
    char* const member = self + field::kAttachments;
    if (Virtual<void*>(member, attachments::kGetOwner)) {
        Virtual<uint32_t>(member, attachments::kStop);
        Virtual<void, void*>(member, attachments::kSetOwner, nullptr);
    }
    return CallThis<uint32_t>(addr::cSC4PrebuiltNetworkOccupant_Shutdown, self);
}

// ====================================================================================
// simulators.bridgeocc.object: construction and destruction
// ====================================================================================

// simulators.bridgeocc.own: with it the objects carry our copies of the
// class's seven vtables (82, 34, 6, 25, 5, 1 and 19 slots) instead of the
// game's tables. The copies are taken at the first use (BuildOwnVtables, with
// the installation: it needs the object group's detour list).
bool g_own = false;
occ_vtables::VtableCopy<82> g_vtable;
occ_vtables::VtableCopy<34> g_vtable4;
occ_vtables::VtableCopy<6> g_vtable8;
occ_vtables::VtableCopy<25> g_vtableC;
occ_vtables::VtableCopy<5> g_vtable20;
occ_vtables::VtableCopy<1> g_vtable38;
occ_vtables::VtableCopy<19> g_vbaseVtable;
void BuildOwnVtables();

// The vtables the constructor and the destructor store (the game's, or with
// simulators.bridgeocc.own our copies of them): the five parts', then the
// virtual base's, found through the table at +0x3C as the game does (the table
// is read twice).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4NetworkBridgeOccupant_vftable;
    At<std::uintptr_t>(self, field::kOccupant) =
        g_own ? g_vtable4.Address() : addr::cSC4NetworkBridgeOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) =
        g_own ? g_vtable8.Address() : addr::cSC4NetworkBridgeOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtableC.Address() : addr::cSC4NetworkBridgeOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) =
        g_own ? g_vtable20.Address() : addr::cSC4NetworkBridgeOccupant_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) =
        g_own ? g_vtable38.Address() : addr::cSC4NetworkBridgeOccupant_vftable_38;
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::cSC4NetworkBridgeOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - 0x1B0;
}

// The constructor (0x00605160; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x3C and the abstract
// vtable at +0x1EC itself). The parent class's constructor with 0, the
// vtables, the member's constructor on +0x1D0, then the two flags set in the
// network flags. The game's EAX is the object.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4NetworkBridgeOccupant_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    CallThis<void*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_cSC4PrebuiltNetworkOccupant, self, 0u);
    SetVtables(self);
    CallThis<void*>(addr::cSC4NetworkBridgeOccupant_AttachmentsConstruct, self + field::kAttachments);
    At<uint32_t>(self, field::kFlags) |= kBridgeFlags;
    return self;
}

// The destructor (0x006053F0; called by the deleting destructor only): the
// vtables as the constructor stores them, the member's destructor on +0x1D0,
// then the parent class's destructor by a tail jump (the game's EAX is what
// that leaves; nobody reads it).
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    CallThis<void>(addr::cSC4NetworkBridgeOccupant_AttachmentsDestroy, self + field::kAttachments);
    CallThis<void>(addr::cSC4PrebuiltNetworkOccupant_Destroy, self);
}

// An item of the bridge pool (0x00605040, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4NetworkBridgeOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// An item of the pool back in its page (the page header is at the address
// with its low 15 bits clear): onto the page's free list, the used count
// down by one; at 0 the page is released, at 0x40000031 opened again.
void PoolReturn(void* pool, char* item)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & 0xFFFF8000u);
    At<void*>(item, 0x0) = At<void*>(page, 0x8);
    const uint32_t used = At<uint32_t>(page, 0xC) - 1;
    At<void*>(page, 0x8) = item;
    At<uint32_t>(page, 0xC) = used;
    if (used == 0)
        CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (used == 0x40000031)
        CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
}

// The deleting destructor (0x00605390, one argument): the destructor, and
// with flag 1 and an object that is not null the memory back to the pool at
// 0x00B0B2D0. The result is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4NetworkBridgeOccupant_Destroy, self);
    if ((flags & 1) && self)
        PoolReturn(reinterpret_cast<void*>(addr::cSC4NetworkBridgeOccupant_Pool), self);
    return self;
}

// The pool's free (0x00605120; slot 1 of the pool's tables 0x00AA7180 and
// 0x00AA6EB8, ECX the pool, one argument: the item; null does nothing).
void __fastcall PoolFree(void* pool, void*, char* item)
{
    static bool logged = false;
    FirstCall("PoolFree", logged);
    if (!item)
        return;
    PoolReturn(pool, item);
}

// The pool's page set-up (0x00604C60; slot 2 of the same tables, and called
// by the pool's static initialiser 0x00A73B30 before any detour is in; ECX
// is not read): a page of 32 KB (the allocator's order 0xF), 66 items of
// 0x1F0 bytes from +0x10 chained upwards (each item's first word is the
// address of the next one, the last has 0), the page's free list at +8 the
// lowest item and its used count at +0xC zero. The result is the page.
char* __cdecl InitPoolPage()
{
    static bool logged = false;
    FirstCall("InitPoolPage", logged);
    char* const page = CallCdecl<char*, uint32_t>(addr::SC4_AllocatePoolPage, 0xFu);
    char* item = page + 0x7E00;
    std::uintptr_t previous = 0;
    char* lowest = nullptr;
    for (int i = 0; i < 0x42; ++i) {
        At<std::uintptr_t>(item, 0) = previous;
        lowest = item;
        previous = reinterpret_cast<std::uintptr_t>(item);
        item -= kObjectSize;
    }
    At<void*>(page, 0x8) = lowest;
    At<uint32_t>(page, 0xC) = 0;
    return page;
}

// The pool allocation (0x00605040; cdecl, the size 0x1F0 pushed by the maker
// function at 0x006242D3 is not read).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (0x00605450): an item of the pool, constructed as the most
// derived object. The result is the occupant part (the object + 4), or null
// if the constructor gave none (it never does).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const object = CallThis<char*, uint32_t>(addr::cSC4NetworkBridgeOccupant_cSC4NetworkBridgeOccupant,
                                                   static_cast<char*>(PoolAllocate()), 1u);
    return object ? object + field::kOccupant : nullptr;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_BRIDGEOCC_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETOCC_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_BRIDGEOCC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("bridgeocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    unsigned installed = 0;
    for (const Detour& detour : detours) {
        if (Skipped(detour.address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
        ++installed;
    }
    log::Write("bridgeocc %s: %u detours", what, installed);
    return true;
}

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_Shutdown, Shutdown),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_cSC4NetworkBridgeOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_InitPoolPage, InitPoolPage),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4NetworkBridgeOccupant_Create, Create),
};

bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.bridgeocc.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly would become ours without the detour; none of this class's
// does (the deleting destructor is behind thunks only), so the count logged is
// 0. Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vftable_C));
    g_vtable20.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vftable_20));
    g_vtable38.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vftable_38));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkBridgeOccupant_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable20,
                                               g_vtable38, g_vbaseVtable);
    }
    log::Write("bridgeocc own: vtables copied (82, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected", redirected);
}

// simulators.bridgeocc.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.bridgeocc.slots", "simulators/bridgeocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkBridgeOccupant_QueryInterface,
                        "Our cSC4NetworkBridgeOccupant small slots: the class ID, QueryInterface, the occupant "
                        "part's Init and Shutdown (4 detours; src/core/bridgeocc_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.bridgeocc.object", "simulators/bridgeocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkBridgeOccupant_cSC4NetworkBridgeOccupant,
                        "Our cSC4NetworkBridgeOccupant constructor, destructor, deleting destructor, the pool's "
                        "free, page set-up and allocation and the factory (7 detours; src/core/bridgeocc_sim.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.bridgeocc.own", "simulators/bridgeocc", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4NetworkBridgeOccupant_vftable,
                        "Our own cSC4NetworkBridgeOccupant: objects carry our copies of the class's seven vtables; "
                        "only has an effect with simulators.bridgeocc.object (src/core/bridgeocc_sim.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::bridgeocc_sim
