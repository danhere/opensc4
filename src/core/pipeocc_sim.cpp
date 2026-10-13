// Our cSC4PipeOccupant (build 641; the class map, the vtables and the fields
// are in src/core/pipeocc_sim.h): the pipe's own functions, each a detour at
// the game's address, because the thunks of the other vtable parts jump to
// them and a maker class calls the constructor and the pool allocation. The
// intermediate class between the pipe and the network occupant (code about
// 0x00652000 to 0x0065D000) is not ours: its functions are called at their
// addresses, with the argument counts below.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither are the two constant answers 0x006484B0 and 0x006484C0 (virtual
// calls only). `self` is the object's start (the interface part) unless a
// function says `part` (Write and Read get the cIGZSerializable part, the
// object + 8; the occupant part's Init the object + 4) or `vbase` (the
// piece key gets the virtual base, the object + 0x27C).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the streams (out vtable 0x00AA2310, in vtable 0x00AA2280): 0x14
//     SetUint8/GetUint8, 0x1C uint16, 0x24 uint32 one argument each, 0x48
//     GetError none;
//   - the object's own slots: 0x4 AddRef none (0x0065A860), 0x24
//     HasAnyNetworkFlag one (0x00604D30), 0x190 the refresh one (0x0065AF10,
//     ret 4); the occupant part's 0x14 IsInitialized none (0x00458A40);
//   - the intermediate class: constructor 0x0065C330 one (the flag, ret 4),
//     destructor 0x0065C0C0 none, Init 0x0065C6B0 three (ret 0xC), occupant
//     Init 0x0065C070 none, Write 0x0065B140 one (ret 4), Read 0x0065E510
//     one (ret 4), QueryInterface 0x0065A080 two (ret 8); cdecl, no
//     arguments: 0x0065AE80 and 0x0065B040;
//   - the vector 0x0048C290 (count, ret 4; this the vector), the allocator's
//     Free 0x0090CF63 (cdecl, one), the page allocator 0x0090D8E0 (cdecl,
//     one), the pool's Grow 0x0090CF9A (none), ReleaseBlock 0x0090D031 and
//     ReopenBlock 0x0090CFF9 (one each, ret 4).
#include "core/pipeocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::pipeocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("pipeocc: first %s", name);
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
// simulators.pipeocc.slots: the small slots
// ====================================================================================

// Slot 0 (0x006484D0, ret 8; the other parts' thunks add or take off their
// offset and jump here): the pipe's IID (its class ID) and 0xA9764C8C give
// the object itself, referenced through slot 1; every other ID goes to the
// intermediate class's QueryInterface by a tail jump. The game's answer in
// the first case is AL = 1 and the rest of EAX from AddRef.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidPipe || iid == kIidObject) {
        *object = self;
        const uint32_t count = Virtual<uint32_t>(self, 0x4);
        return (count & 0xFFFFFF00u) | 1u;
    }
    return CallThis<uint32_t, uint32_t, void**>(addr::SC4PipeBase_QueryInterface, self, iid, object);
}

// Slot 101 (0x006484B0): the address of the descriptor the intermediate
// class's occupant Init stores at +0x26C. Only EAX is touched.
std::uintptr_t __fastcall GetDescriptor(void*, void*)
{
    static bool logged = false;
    FirstCall("GetDescriptor", logged);
    return addr::cSC4PipeOccupant_Descriptor;
}

// Slot 105 (0x006484C0): the flags a new piece starts with (0x06000000).
// Only EAX is touched.
uint32_t __fastcall GetDefaultFlags(void*, void*)
{
    static bool logged = false;
    FirstCall("GetDefaultFlags", logged);
    return 0x06000000u;
}

// The virtual base's slot 17 (0x00648500 behind the thunk 0x006487B0, which
// takes the base's displacement word, zero, off the pointer; `vbase` is the
// object + 0x27C; one argument, the out word): when the piece has any of the
// bits 0x06000000 in its network flags (slot 9) the word at +0xFC, else the
// class ID. The game's EAX is slot 9's answer in the first case and the out
// pointer in the second.
uint32_t __fastcall GetPieceKey(char* vbase, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetPieceKey", logged);
    char* const self = vbase - field::kVbase;
    const uint8_t any = Virtual<uint8_t>(self, 0x24, 0x06000000u);
    if (any) {
        *out = At<uint32_t>(self, field::kPieceKey);
        return any;
    }
    *out = kClassId;
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(out));
}

// 0x00648540 (called by the water simulator; one argument, a byte): the
// watered word is 1 or 0 after it. When the byte equals (word == 1) nothing
// else happens (EAX is the byte). Otherwise the word is stored (the byte
// non-zero) and the refresh, slot 100 of the primary vtable, is jumped to
// with the argument replaced by 0.
uint32_t __fastcall SetWatered(char* self, void*, uint32_t watered)
{
    static bool logged = false;
    FirstCall("SetWatered", logged);
    const uint8_t wanted = static_cast<uint8_t>(watered);
    const uint8_t current = At<uint32_t>(self, field::kWatered) == 1 ? 1 : 0;
    if (wanted == current)
        return wanted;
    At<uint32_t>(self, field::kWatered) = wanted != 0 ? 1u : 0u;
    return Virtual<uint32_t>(self, slot::kRefresh, 0u);
}

// Slot 103 (0x006488A0; one argument): the three words of the tint for the
// watered word (a table of 12-byte records at 0x00B0BE88, the word is the
// index) copied out. EAX is the out pointer.
uint32_t* __fastcall GetTint(char* self, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetTint", logged);
    const uint32_t offset = At<uint32_t>(self, field::kWatered) * 12u;
    const auto* const tint = reinterpret_cast<const uint32_t*>(addr::cSC4PipeOccupant_Tints + offset);
    out[0] = tint[0];
    out[1] = tint[1];
    out[2] = tint[2];
    return out;
}

// ====================================================================================
// simulators.pipeocc.init: the statics and the two Inits
// ====================================================================================

// 0x00648A90 (cdecl, none): set up once, marked by the byte at 0x00B46730.
// Five distances between the points of a table (0x00B0BA68, 24-byte records
// whose x is at +0 and z at +8) picked by two index tables (0x00B0BC60 and
// 0x00B0BC88), sqrt(dx * dx + dz * dz) as the FPU makes it (53-bit
// precision, so double) and stored as floats in a vector of five (made and
// freed by the game's vector and allocator). Then the ratios of them in the
// statics (0x00B46708 to 0x00B4672C: ones, and the ratios v1/(v1+v4),
// v2/(v3+v2), v3/(v2+v3) and v4/(v1+v4) twice each), the done byte, and,
// when the network occupant's statics (0x00B45250) are not done, the
// intermediate class's 0x0065B040, whose answer is the result. Otherwise 1.
// Already done: 0.
uint8_t __cdecl InitStatics()
{
    static bool logged = false;
    FirstCall("InitStatics", logged);
    const std::uintptr_t statics = addr::cSC4PipeOccupant_Statics;
    if (Global<uint8_t>(statics + kStaticsDone))
        return 0;
    uint32_t vector[3];
    CallThis<void*, uint32_t>(addr::SC4DwordVector_ConstructZeroed, vector, 5u);
    float* const values = reinterpret_cast<float*>(vector[0]);
    for (uint32_t i = 0; i < 5; ++i) {
        const uint32_t from = Global<uint32_t>(addr::cSC4PipeOccupant_FromPoints + 4 * i) * 24u;
        const uint32_t to = Global<uint32_t>(addr::cSC4PipeOccupant_ToPoints + 4 * i) * 24u;
        const std::uintptr_t a = addr::cSC4PipeOccupant_Points + from;
        const std::uintptr_t b = addr::cSC4PipeOccupant_Points + to;
        const double dx = static_cast<double>(Global<float>(a)) - static_cast<double>(Global<float>(b));
        const double dz = static_cast<double>(Global<float>(a + 8)) - static_cast<double>(Global<float>(b + 8));
        values[i] = static_cast<float>(std::sqrt(dx * dx + dz * dz));
    }
    const float one = 1.0f;
    Global<float>(statics + 0x14) = one;
    Global<float>(statics + 0x10) = one;
    const uint8_t networkDone = Global<uint8_t>(addr::cSC4NetworkOccupant_StaticsDone);
    const double v1 = static_cast<double>(values[1]), v2 = static_cast<double>(values[2]);
    const double v3 = static_cast<double>(values[3]), v4 = static_cast<double>(values[4]);
    const float first = static_cast<float>(v1 / (v1 + v4));
    Global<float>(statics + 0x18) = first;
    Global<float>(statics + 0xC) = first;
    const float second = static_cast<float>(v2 / (v3 + v2));
    Global<float>(statics + 0x1C) = second;
    Global<float>(statics + 0x8) = second;
    const float third = static_cast<float>(v3 / (v2 + v3));
    Global<float>(statics + 0x20) = third;
    Global<float>(statics + 0x4) = third;
    Global<uint8_t>(statics + kStaticsDone) = 1;
    const float fourth = static_cast<float>(v4 / (v1 + v4));
    Global<float>(statics + 0x24) = fourth;
    Global<float>(statics + 0x0) = fourth;
    if (!networkDone) {
        const uint8_t result = CallCdecl<uint8_t>(addr::SC4PipeBase_InitStatics);
        CallCdecl<void, void*>(addr::SC4_Free, values);
        return result;
    }
    CallCdecl<void, void*>(addr::SC4_Free, values);
    return 1;
}

// The occupant part's slot 3 (0x00648BC0; `part` is the object + 4): the
// statics first (when not done), and an object that is not initialised yet
// goes through the intermediate class's occupant Init (on the part), then
// 0x0065AE80, which reads no register. AL is 1.
uint8_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    if (!Global<uint8_t>(addr::cSC4PipeOccupant_Statics + kStaticsDone))
        CallCdecl<uint8_t>(addr::cSC4PipeOccupant_InitStatics);
    if (!Virtual<uint8_t>(part, 0x14)) {
        CallThis<uint8_t>(addr::SC4PipeBase_OccupantInit, part);
        CallCdecl<uint8_t>(addr::SC4PipeBase_InitModels);
    }
    return 1;
}

// The interface's slot 3 (0x00648BF0, one argument, handed on): the statics
// first (when not done); an object that is not initialised yet goes through
// the intermediate class's Init with the argument, the class ID and the
// descriptor, and its watered word is cleared. AL is 1.
uint8_t __fastcall Init(char* self, void*, void* description)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (!Global<uint8_t>(addr::cSC4PipeOccupant_Statics + kStaticsDone))
        CallCdecl<uint8_t>(addr::cSC4PipeOccupant_InitStatics);
    if (!Virtual<uint8_t>(self + field::kOccupant, 0x14)) {
        CallThis<uint8_t, void*, uint32_t, uint32_t>(addr::SC4PipeBase_Init, self, description, kClassId,
                                                     static_cast<uint32_t>(addr::cSC4PipeOccupant_Descriptor));
        At<uint32_t>(self, field::kWatered) = 0;
    }
    return 1;
}

// ====================================================================================
// simulators.pipeocc.save: the save record
// ====================================================================================

// +0x8 slot 3 (0x00648800; `part` is the object + 8). Version 3, then the
// intermediate class's record (which holds the network occupant's: when it
// fails the pipe's word is left out and the answer is still the stream's
// state), then the watered word (object + 0x270). The layout is in
// tools/re/save_schemas/pipeocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (CallThis<uint8_t, void*>(addr::SC4PipeBase_Write, part, stream))
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(part, field::kWatered - field::kSerializable));
    return NoError(stream);
}

// +0x8 slot 4 (0x00648840; `part` is the object + 8): version 3 only. The
// game reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. The intermediate class's record, and when that is good the
// watered word, read into a local slot that holds the part's address until
// the stream writes it (the `push ecx` of the game's frame), stored in the
// object; and the byte at +0x260 set to 1. The answer is the stream's state.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::SC4PipeBase_Read, part, stream)) {
        uint32_t value = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
        Virtual<uint8_t>(stream, 0x24, &value);
        At<uint32_t>(part, field::kWatered - field::kSerializable) = value;
        At<uint8_t>(part, field::kDirty - field::kSerializable) = 1;
    }
    return NoError(stream);
}

// ====================================================================================
// simulators.pipeocc.object: construction and destruction
// ====================================================================================

// simulators.pipeocc.own: with it the objects carry our copies of the class's
// seven vtables (106, 34, 6, 25, 5, 1 and 19 slots) instead of the game's
// tables. The copies are taken at the first use (BuildOwnVtables, with the
// installation: it needs the object group's detour list).
bool g_own = false;
occ_vtables::VtableCopy<106> g_vtable;
occ_vtables::VtableCopy<34> g_vtable4;
occ_vtables::VtableCopy<6> g_vtable8;
occ_vtables::VtableCopy<25> g_vtableC;
occ_vtables::VtableCopy<5> g_vtable20;
occ_vtables::VtableCopy<1> g_vtable38;
occ_vtables::VtableCopy<19> g_vbaseVtable;
void BuildOwnVtables();

// The vtables the constructor and the destructor store (the game's, or with
// simulators.pipeocc.own our copies of them): the five parts', then the
// virtual base's, found through the table at +0x3C as the game does (the table
// is read twice).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4PipeOccupant_vftable;
    At<std::uintptr_t>(self, field::kOccupant) = g_own ? g_vtable4.Address() : addr::cSC4PipeOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) = g_own ? g_vtable8.Address() : addr::cSC4PipeOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtableC.Address() : addr::cSC4PipeOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) = g_own ? g_vtable20.Address() : addr::cSC4PipeOccupant_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) = g_own ? g_vtable38.Address() : addr::cSC4PipeOccupant_vftable_38;
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::cSC4PipeOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - 0x240;
}

// The constructor (0x006488F0; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x3C and the abstract
// vtable at +0x27C itself). The intermediate class's constructor with 0,
// the vtables, the watered word zero. The game's EAX is the object.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4PipeOccupant_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    CallThis<void*, uint32_t>(addr::SC4PipeBase_cSC4PipeBase, self, 0u);
    SetVtables(self);
    At<uint32_t>(self, field::kWatered) = 0;
    return self;
}

// An item of the pipe pool (0x006487D0, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4PipeOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// An item of the pool back in its page (the page header is at the address
// with its low 16 bits clear): onto the page's free list, the used count
// down by one; at 0 the page is released, at 0x4000004C opened again.
void PoolReturn(void* pool, char* item)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & 0xFFFF0000u);
    At<void*>(item, 0x0) = At<void*>(page, 0x8);
    const uint32_t used = At<uint32_t>(page, 0xC) - 1;
    At<void*>(page, 0x8) = item;
    At<uint32_t>(page, 0xC) = used;
    if (used == 0)
        CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (used == 0x4000004C)
        CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
}

// The deleting destructor (0x006489F0, one argument): the vtables as the
// constructor stores them, the intermediate class's destructor, and with
// flag 1 the memory back to the pool at 0x00B0BEC0 (no test for null). The
// result is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    CallThis<void>(addr::SC4PipeBase_Destroy, self);
    if (flags & 1)
        PoolReturn(reinterpret_cast<void*>(addr::cSC4PipeOccupant_Pool), self);
    return self;
}

// The pool's free (0x00648970; slot 1 of the pool's table at 0x00AA9960, ECX
// the pool, one argument: the item; null does nothing).
void __fastcall PoolFree(void* pool, void*, char* item)
{
    static bool logged = false;
    FirstCall("PoolFree", logged);
    if (!item)
        return;
    PoolReturn(pool, item);
}

// The pool's page set-up (0x00648580; slot 2 of the same table; ECX is not
// read): a page of 64 KB (the allocator's order 0x10), 102 items of 0x280
// bytes from +0x10 chained upwards (each item's first word is the address of
// the next one, the last has 0), the page's free list at +8 the lowest item
// and its used count at +0xC zero. The result is the page.
char* __cdecl InitPoolPage()
{
    static bool logged = false;
    FirstCall("InitPoolPage", logged);
    char* const page = CallCdecl<char*, uint32_t>(addr::SC4_AllocatePoolPage, 0x10u);
    char* item = page + 0xFC90;
    std::uintptr_t previous = 0;
    char* lowest = nullptr;
    for (int i = 0; i < 0x66; ++i) {
        At<std::uintptr_t>(item, 0) = previous;
        lowest = item;
        previous = reinterpret_cast<std::uintptr_t>(item);
        item -= kObjectSize;
    }
    At<void*>(page, 0x8) = lowest;
    At<uint32_t>(page, 0xC) = 0;
    return page;
}

// The pool allocation (0x006487D0; cdecl, the size 0x280 pushed by the maker
// class is not read).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (0x006489B0): an item of the pool, constructed as the most
// derived object. The result is the occupant part (the object + 4), or null
// if the constructor gave none (it never does).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const object = static_cast<char*>(Construct(static_cast<char*>(PoolAllocate()), nullptr, 1u));
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

// Hooks to leave out, for bisecting: OPENSC4_PIPEOCC_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETOCC_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_PIPEOCC_SKIP", list, sizeof list))
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
            log::Write("pipeocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
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
    log::Write("pipeocc %s: %u detours", what, installed);
    return true;
}

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_GetDescriptor, GetDescriptor),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_GetDefaultFlags, GetDefaultFlags),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_GetPieceKey, GetPieceKey),
    OPENSC4_DETOUR(addr::SC4Pipe_SetWatered, SetWatered),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_GetTint, GetTint),
};

const Detour kInit[] = {
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_InitStatics, InitStatics),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_Init, Init),
};

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_Read, Read),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_cSC4PipeOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_InitPoolPage, InitPoolPage),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4PipeOccupant_Create, Create),
};

bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }
bool InstallInit(registry::Mode) { return InstallDetours(kInit, "init"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.pipeocc.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly would become ours without the detour; none of this class's
// does (the deleting destructor is behind thunks only), so the count logged is
// 0. Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vftable_C));
    g_vtable20.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vftable_20));
    g_vtable38.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vftable_38));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::cSC4PipeOccupant_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable20,
                                               g_vtable38, g_vbaseVtable);
    }
    log::Write("pipeocc own: vtables copied (106, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected", redirected);
}

// simulators.pipeocc.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.pipeocc.slots", "simulators/pipeocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PipeOccupant_QueryInterface,
                        "Our cSC4PipeOccupant small slots: QueryInterface, the descriptor, the default flags, the "
                        "virtual base's piece key, SetWatered and the tint (6 detours; src/core/pipeocc_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.pipeocc.init", "simulators/pipeocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PipeOccupant_InitStatics,
                        "Our cSC4PipeOccupant statics (0x00648A90) and the two Inits (3 detours; "
                        "src/core/pipeocc_sim.cpp)",
                        false, InstallInit}),
    registry::Register({"simulators.pipeocc.save", "simulators/pipeocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PipeOccupant_Write,
                        "Our cSC4PipeOccupant save record: Write and Read (version 3; 2 detours; "
                        "src/core/pipeocc_sim.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.pipeocc.object", "simulators/pipeocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PipeOccupant_cSC4PipeOccupant,
                        "Our cSC4PipeOccupant constructor, deleting destructor, the pool's free, page set-up and "
                        "allocation and the factory (6 detours; src/core/pipeocc_sim.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.pipeocc.own", "simulators/pipeocc", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4PipeOccupant_vftable,
                        "Our own cSC4PipeOccupant: objects carry our copies of the class's seven vtables; only has an "
                        "effect with simulators.pipeocc.object (src/core/pipeocc_sim.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::pipeocc_sim
