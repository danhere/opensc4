// Our cSC4NetworkTunnelOccupant (build 641; the class map, the vtables and
// the fields are in src/core/tunnelocc_sim.h): the tunnel entrance's own
// functions, each a detour at the game's address, because the thunks of the
// other vtable parts jump to them and the network tool calls the constructor,
// the pool allocation and the setter directly. The parent class (the
// pre-built-model network occupant, code about 0x0061D000 to 0x0061F500 and
// 0x00604CA0) is not this file's: its functions are called at their
// addresses, with the argument counts below.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither are GetGZCLSID 0x006472D0 (only EAX; besides the virtual calls it
// is called at 0x0046075A, where ECX is loaded again before it is used and
// EDX is not read) and the pool allocation 0x00647230 when the page has a
// free item (EDX is used, ECX too). `self` is the object's start (the
// interface part) unless a function says `part` (Write and Read get the
// cIGZSerializable part, the object + 8; the occupant part's Init the
// object + 4).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the streams (out vtable 0x00AA2310, in vtable 0x00AA2280): 0x14
//     SetUint8/GetUint8 and 0x1C SetUint16/GetUint16 one argument each, 0x48
//     GetError none;
//   - the object's own slots: 0x4 AddRef none (0x006471A0, a jump to
//     0x0065A860), 0x2C ClearNetworkFlag one (0x00619F70, ret 4); the
//     occupant part's 0x14 IsInitialized none (0x00458A40, which answers 0
//     or 1 in all of EAX);
//   - the other entrance and its serializable part: 0x0 QueryInterface two
//     (0x00647170, ret 8), 0x4 AddRef and 0x8 Release none;
//   - the parent class: constructor 0x0061E1A0 one (the flag, ret 4),
//     destructor 0x00604CA0 none (it ends in a jump to the network
//     occupant's 0x0061C170), QueryInterface 0x0061D060 two (ret 8), slot 4
//     0x0061D0D0 none (plain ret), the occupant part's Init 0x0061DB00 none
//     (plain ret), Write 0x0061D8E0 and Read 0x0061D9B0 one each (ret 4);
//   - the references: 0x005D99A0 (cdecl, two: the stream and the object;
//     add esp, 8) and 0x005D9730 (cdecl, four: the stream, the IID, the out
//     pointer and 0; add esp, 0x10);
//   - the page allocator 0x0090D8E0 (cdecl, one; add esp, 4), the pool's
//     Grow 0x0090CF9A (none), ReleaseBlock 0x0090D031 and ReopenBlock
//     0x0090CFF9 (one each, ret 4).
#include "core/tunnelocc_sim.h"

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

namespace opensc4::tunnelocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("tunnelocc: first %s", name);
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

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// ====================================================================================
// simulators.tunnelocc.slots: the small slots and the setter
// ====================================================================================

// Slot 0 (0x00647170, ret 8; the other parts' thunks take off their offset
// and jump here): the tunnel's IID (its class ID) gives the object itself,
// referenced through slot 1; every other ID goes to the parent's
// QueryInterface by a tail jump (which answers its own IID the same way and
// hands the rest to the network occupant's). The game's answer in the first
// case is AL = 1 and the rest of EAX from AddRef.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidTunnel) {
        *object = self;
        const uint32_t count = Virtual<uint32_t>(self, slot::kAddRef);
        return (count & 0xFFFFFF00u) | 1u;
    }
    return CallThis<uint32_t, uint32_t, void**>(parent::kQueryInterface, self, iid, object);
}

// The serializable part's slot 5 (0x006472D0; also called directly by the
// class registration at 0x0046075A): the class ID. Only EAX is touched.
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// The occupant part's slot 3 (0x006471C0; `part` is the object + 4): an
// object that is initialised already answers 1 (AL set in IsInitialized's
// EAX). Otherwise the parent's occupant Init on the part, whose AL is the
// answer, and after it the network flag 0x4000 is cleared through slot 11 of
// the interface (whatever the parent answered); the rest of EAX is that
// call's.
uint32_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    const uint32_t initialized = Virtual<uint32_t>(part, slot::kIsInitialized);
    if (static_cast<uint8_t>(initialized))
        return (initialized & 0xFFFFFF00u) | 1u;
    const uint8_t result = CallThis<uint8_t>(parent::kOccupantInit, part);
    char* const self = part - field::kOccupant;
    const uint32_t cleared = Virtual<uint32_t>(self, slot::kClearNetworkFlag, kFlagTranslucent);
    return (cleared & 0xFFFFFF00u) | result;
}

// Slot 4 (0x006474F0; the occupant part's slot 4 is the thunk 0x006476C0 to
// it): an object that is not initialised answers 1 (AL set in
// IsInitialized's EAX). Otherwise the other entrance is let go (the field
// zeroed first, then its Release) and the parent's slot 4 is jumped to,
// whose answer is the result.
uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    const uint32_t initialized = Virtual<uint32_t>(self + field::kOccupant, slot::kIsInitialized);
    if (!static_cast<uint8_t>(initialized))
        return (initialized & 0xFFFFFF00u) | 1u;
    if (void* const other = At<void*>(self, field::kOtherEnd)) {
        At<void*>(self, field::kOtherEnd) = nullptr;
        Virtual<uint32_t>(other, slot::kRelease);
    }
    return CallThis<uint32_t>(parent::kShutdown, self);
}

// 0x00647530 (ret 4; called twice at 0x00628AFE and 0x00628B08, once for
// each entrance of a new tunnel): the other entrance. The same pointer
// again does nothing. Otherwise the new one is referenced (when not null),
// stored, and the old one released (when not null), in that order. The game
// leaves no answer.
void __fastcall SetOtherEnd(char* self, void*, void* other)
{
    static bool logged = false;
    FirstCall("SetOtherEnd", logged);
    void* const old = At<void*>(self, field::kOtherEnd);
    if (other == old)
        return;
    if (other)
        Virtual<uint32_t>(other, slot::kAddRef);
    At<void*>(self, field::kOtherEnd) = other;
    if (old)
        Virtual<uint32_t>(old, slot::kRelease);
}

// ====================================================================================
// simulators.tunnelocc.save: the save record
// ====================================================================================

// +0x8 slot 3 (0x00647580; `part` is the object + 8). Version 2, then the
// parent's record (which holds the network occupant's: when it fails
// nothing more is written and the answer is still the stream's state). Then
// the other entrance: when there is one and it answers the serializable IID
// 0xE4FDA3D4, the byte 1 and a reference to that part (0x005D99A0);
// otherwise the byte 0. What the query stored is released when it is not
// null, whether or not the query said yes. The layout is in
// tools/re/save_schemas/tunnelocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (CallThis<uint8_t, void*>(parent::kWrite, part, stream)) {
        void* const other = At<void*>(part, field::kOtherEnd - field::kSerializable);
        void* serializable = nullptr;
        if (other && Virtual<uint8_t>(other, 0x0, kIidSerializable, &serializable)) {
            Virtual<uint8_t>(stream, 0x14, 1u);
            CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, serializable);
        } else {
            Virtual<uint8_t>(stream, 0x14, 0u);
        }
        if (serializable)
            Virtual<uint32_t>(serializable, slot::kRelease);
    }
    return NoError(stream);
}

// +0x8 slot 4 (0x00647610; `part` is the object + 8): version 2 only. The
// game reads the version into a slot that holds the part's address until
// the stream writes two bytes of it (the `push ecx` of its frame), so it
// starts with that value here. Another version: AL cleared, the rest of EAX
// as the stream's read left it. Then the parent's record; when that is
// good, the byte, read into the argument's slot (the stream pointer until
// the stream writes one byte of it). Non-zero: the other entrance the
// object holds is let go (the field zeroed first) and the reference read
// into the field (0x005D9730 with the tunnel's IID and 0). Zero: the other
// entrance is let go the same way. The answer is the stream's state.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(parent::kRead, part, stream)) {
        uint32_t has = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
        Virtual<uint8_t>(stream, 0x14, &has);
        void** const otherEnd = &At<void*>(part, field::kOtherEnd - field::kSerializable);
        if (void* const old = *otherEnd) {
            *otherEnd = nullptr;
            Virtual<uint32_t>(old, slot::kRelease);
        }
        if (static_cast<uint8_t>(has))
            CallCdecl<uint8_t, void*, uint32_t, void**, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidTunnel,
                                                                  otherEnd, 0u);
    }
    return NoError(stream);
}

// ====================================================================================
// simulators.tunnelocc.object: construction and destruction
// ====================================================================================

// simulators.tunnelocc.own: with it the objects carry our copies of the
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
// simulators.tunnelocc.own our copies of them): the six parts', then the
// virtual base's, found through the table at +0x3C as the game does (the table
// is read twice), and the virtual base's displacement word (the displacement
// less 0x1A0: zero).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4NetworkTunnelOccupant_vftable;
    At<std::uintptr_t>(self, field::kOccupant) =
        g_own ? g_vtable4.Address() : addr::cSC4NetworkTunnelOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) =
        g_own ? g_vtable8.Address() : addr::cSC4NetworkTunnelOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtableC.Address() : addr::cSC4NetworkTunnelOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) =
        g_own ? g_vtable20.Address() : addr::cSC4NetworkTunnelOccupant_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) =
        g_own ? g_vtable38.Address() : addr::cSC4NetworkTunnelOccupant_vftable_38;
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::cSC4NetworkTunnelOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - 0x1A0;
}

// The constructor (0x006476F0, ret 4; one argument: non-zero for the most
// derived object, which sets the virtual base table at +0x3C and the
// abstract vtable at +0x1DC itself). The parent's constructor with 0, the
// vtables, the other entrance null, and in the network flags 0x4000 cleared
// and 0x10C00000 set. The game's EAX is the object.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4NetworkTunnelOccupant_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    CallThis<void*, uint32_t>(parent::kConstruct, self, 0u);
    SetVtables(self);
    At<void*>(self, field::kOtherEnd) = nullptr;
    At<uint32_t>(self, field::kFlags) = (At<uint32_t>(self, field::kFlags) & ~kFlagTranslucent) | kFlagsConstructed;
    return self;
}

// The destructor (0x00647260, none; only the deleting destructor calls
// it): the vtables as the constructor stores them, the other entrance
// released when there is one (the field is not zeroed), then the parent's
// destructor by a tail jump, whose EAX is the result.
uint32_t __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    if (void* const other = At<void*>(self, field::kOtherEnd))
        Virtual<uint32_t>(other, slot::kRelease);
    return CallThis<uint32_t>(parent::kDestroy, self);
}

// An item of the tunnel pool (0x00647230, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4NetworkTunnelOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x00647790, ret 4; one argument): the destructor
// (at its address, 0x00647260), and with flag 1 and an object that is not
// null the memory back to its pool page (the page header is at the address
// with its low 15 bits clear): onto the page's free list, the used count
// down by one; at 0 the page is released, at 0x40000033 opened again, both
// on the pool at 0x00B0B570. The result is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<uint32_t>(addr::cSC4NetworkTunnelOccupant_Destroy, self);
    if ((flags & 1) && self) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & kPageMask);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4NetworkTunnelOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == kReopenCount)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The pool's page set-up (0x006471F0; slot 2 of the pool's table at
// 0x00AA9188 and of its copy at 0x00AA9458, and called by the static
// initialiser 0x00A73DE0; ECX is not read): a page of 32 KB (the
// allocator's order 0xF), 68 items of 0x1E0 bytes from +0x10 chained
// upwards (each item's first word is the address of the next one, the last
// has 0), the page's free list at +8 the lowest item and its used count at
// +0xC zero. The result is the page.
char* __cdecl InitPoolPage()
{
    static bool logged = false;
    FirstCall("InitPoolPage", logged);
    char* const page = CallCdecl<char*, uint32_t>(addr::SC4_AllocatePoolPage, 0xFu);
    char* item = page + 0x7DB0;
    std::uintptr_t previous = 0;
    char* lowest = nullptr;
    for (uint32_t i = 0; i < kPageItems; ++i) {
        At<std::uintptr_t>(item, 0) = previous;
        lowest = item;
        previous = reinterpret_cast<std::uintptr_t>(item);
        item -= kObjectSize;
    }
    At<void*>(page, 0x8) = lowest;
    At<uint32_t>(page, 0xC) = 0;
    return page;
}

// The pool allocation (0x00647230; cdecl, the size 0x1E0 pushed at
// 0x006242B5 is not read).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (0x006477F0): an item of the pool, constructed as the most
// derived object by the constructor at its address (0x006476F0). The result
// is the occupant part (the object + 4), or null if the constructor gave
// none (it never does).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const object =
        CallThis<char*, uint32_t>(addr::cSC4NetworkTunnelOccupant_cSC4NetworkTunnelOccupant, PoolAllocate(), 1u);
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

// Hooks to leave out, for bisecting: OPENSC4_TUNNELOCC_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETOCC_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_TUNNELOCC_SKIP", list, sizeof list))
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
            log::Write("tunnelocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
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
    log::Write("tunnelocc %s: %u detours", what, installed);
    return true;
}

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_SetOtherEnd, SetOtherEnd),
};

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_Read, Read),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_cSC4NetworkTunnelOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_InitPoolPage, InitPoolPage),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4NetworkTunnelOccupant_Create, Create),
};

bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.tunnelocc.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly would become ours without the detour; none of this class's
// does (the deleting destructor is behind thunks only), so the count logged is
// 0. Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vftable_C));
    g_vtable20.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vftable_20));
    g_vtable38.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vftable_38));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkTunnelOccupant_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable20,
                                               g_vtable38, g_vbaseVtable);
    }
    log::Write("tunnelocc own: vtables copied (82, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected", redirected);
}

// simulators.tunnelocc.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.tunnelocc.slots", "simulators/tunnelocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTunnelOccupant_QueryInterface,
                        "Our cSC4NetworkTunnelOccupant small slots: QueryInterface, GetGZCLSID, the occupant part's "
                        "Init, slot 4 and the other entrance's setter (5 detours; src/core/tunnelocc_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.tunnelocc.save", "simulators/tunnelocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTunnelOccupant_Write,
                        "Our cSC4NetworkTunnelOccupant save record: Write and Read (version 2; 2 detours; "
                        "src/core/tunnelocc_sim.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.tunnelocc.object", "simulators/tunnelocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTunnelOccupant_cSC4NetworkTunnelOccupant,
                        "Our cSC4NetworkTunnelOccupant constructor, destructor, deleting destructor, the pool's page "
                        "set-up and allocation and the factory (6 detours; src/core/tunnelocc_sim.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.tunnelocc.own", "simulators/tunnelocc", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTunnelOccupant_vftable,
                        "Our own cSC4NetworkTunnelOccupant: objects carry our copies of the class's seven vtables; "
                        "only has an effect with simulators.tunnelocc.object (src/core/tunnelocc_sim.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::tunnelocc_sim
