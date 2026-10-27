// Our pedestrians, part ped1 (build 641): cSC4Pedestrian on the automaton base
// (src/core/automata_vehicle.cpp) and cSC4PedestrianOneShot, cSC4RiotPolice
// and cSC4SimFireCrew on it. ped1: the objects (the four factories, the
// allocator and its release, the pool's free, the four constructors, the two
// QueryInterfaces, the four deleting destructors and the fire crew's
// destructor body, the four GetGZCLSIDs), the save (the pedestrian's, the
// riot police's and the fire crew's Writes and Reads) and the small queries
// (the pedestrian's slots 16, 17 and 174, the one-shot's 177 and 178 and the
// crews' word +0x274). The layout is in src/core/automata_pedestrians.h and
// the survey in docs/automata.md, "Part ped". Calls between functions of the
// part go to the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.pedestrians: the 34 detours in kObject (ped1) below.
//
// What it does. A pedestrian is a 0x270-byte block of the pool 0x00B0A018
// (0x00544890; the one-shot, the riot police and the fire crew, of other
// sizes, come from SC4::operator_new there) and goes back through 0x00545130
// by its size. The pedestrian's constructor runs the automaton base's with
// its four words, sets its six vtables, its fields (the point three copies of
// the float 0x00B228DC, +0x26C 0.01), the occupant type 0x496E5783, and a
// time drawn from the core's generator into +0x80 and the smoothed value as
// cSC4Vehicle's does. The one-shot's sets the byte +0x270 to 5; the riot
// police's and the fire crew's take the vehicle that sent them (held at
// +0x270) and their word; the fire crew's makes the fire truck's two occupant
// filters.
//
// Oddities kept: the riot police and the fire crew answer the pedestrian's
// QueryInterface (their slot 0 is a jump to it) and so not their own class
// IDs; the one-shot's is its own and the pedestrian's; the pedestrian's and
// the one-shot's deleting destructors inline the automaton base's body (ours
// calls it); the one-shot's goes back through SC4::operator_delete, the
// crews' through 0x00545130; the fire crew's +0x2B4 is saved but never set
// by its constructor; the Writes and Reads answer whether the stream's error
// (slot 18, its AL) is zero, not what the inner Write or Read answered, but
// a Read of the wrong version answers 0 before it; the pedestrian's Read
// reads +0x254 through a local that holds the part's address when the read
// fails, the crews' read +0x274 through a local the game leaves unset (ours
// zero); slot 16 places the pedestrian with the heading's y zeroed. AL only
// in the game: QueryInterface's true, the Reads' false, slot 177; ours
// zero-extends.
//
// Floats: the constructor's time is the drawn int (FILD, signed) times the
// float 0x00A9A3FC, stored as a float (at 53 bits, as cSC4Vehicle's); slot
// 17 answers the float 0.5 0x00A84D2C in ST0.
//
// Still the game's, at their addresses: the automaton base's code (part vb),
// the riot police's and the fire crew's jumps to the pedestrian's
// QueryInterface and the adjustor thunks the vtables hold, the occupant
// filters' code, the stream helpers, the pool's growth and release, and the
// pedestrians' states, frames, Inits and Shutdowns (parts ped2 on).

#include "core/automata_pedestrians.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_pedestrians {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename T>
inline T Global(std::uintptr_t address)
{
    return *reinterpret_cast<const T*>(address);
}

// A virtual call through the object's first vtable, `byteOffset` in bytes.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("automata pedestrians: first %s", name);
}

constexpr std::size_t kSerializable = 0x08;
// The automaton base's fields this part writes.
constexpr std::size_t kType = 0x28, kDraw = 0x80, kSmoothed = 0x1A0;

std::uint32_t Release(void* object) { return Virtual<std::uint32_t>(object, slot::kRelease); }

// A field holding a reference given `value`: held (AddRef) when it differs,
// the old one released after the store.
void Assign(char* self, std::size_t at, void* value)
{
    void* const old = At<void*>(self, at);
    if (value == old)
        return;
    if (value)
        Virtual<std::uint32_t>(value, slot::kAddRef);
    At<void*>(self, at) = value;
    if (old)
        Release(old);
}

// The six vtables a constructor sets (+0x00, +0x04, +0x08, +0x0C, +0x20,
// +0x38).
void SetVtables(char* self, const std::uintptr_t (&tables)[6])
{
    static constexpr std::size_t kParts[6] = {0x00, 0x04, 0x08, 0x0C, 0x20, 0x38};
    for (int i = 0; i < 6; ++i)
        At<std::uintptr_t>(self, kParts[i]) = tables[i];
}

// A stream slot with one argument (its bool answer not read, as the game's).
template <typename... Args>
void StreamCall(void* s, std::size_t at, Args... args)
{
    Virtual<std::uint8_t>(s, at, args...);
}

namespace stream {
constexpr std::size_t kUint8 = 0x14, kUint16 = 0x1C, kSint32 = 0x20, kUint32 = 0x24, kFloat32 = 0x30;
} // namespace stream

// The version word of a Read that reads it into the stream argument's own
// stack slot: what is there when the read fails is the address's low word.
std::uint16_t VersionInArgument(void* s)
{
    std::uint16_t version = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(s));
    StreamCall(s, stream::kUint16, &version);
    return version;
}

// What the Writes and Reads answer: whether the stream's error (its AL) is
// zero.
std::uint32_t StreamGood(void* s) { return (Virtual<std::uint32_t>(s, slot::kStreamError) & 0xFF) == 0 ? 1u : 0u; }

// ====================================================================================
// The objects

constexpr std::uintptr_t kPedestrianTables[6] = {addr::cSC4Pedestrian_Vtable,   addr::cSC4Pedestrian_Vtable4,
                                                 addr::cSC4Pedestrian_Vtable8,  addr::cSC4Pedestrian_VtableC,
                                                 addr::cSC4Pedestrian_Vtable20, addr::cSC4Pedestrian_Vtable38};
constexpr std::uintptr_t kOneShotTables[6] = {
    addr::cSC4PedestrianOneShot_Vtable,  addr::cSC4PedestrianOneShot_Vtable4,  addr::cSC4PedestrianOneShot_Vtable8,
    addr::cSC4PedestrianOneShot_VtableC, addr::cSC4PedestrianOneShot_Vtable20, addr::cSC4PedestrianOneShot_Vtable38};
constexpr std::uintptr_t kRiotTables[6] = {addr::cSC4RiotPolice_Vtable,   addr::cSC4RiotPolice_Vtable4,
                                           addr::cSC4RiotPolice_Vtable8,  addr::cSC4RiotPolice_VtableC,
                                           addr::cSC4RiotPolice_Vtable20, addr::cSC4RiotPolice_Vtable38};
constexpr std::uintptr_t kFireTables[6] = {addr::cSC4SimFireCrew_Vtable,   addr::cSC4SimFireCrew_Vtable4,
                                           addr::cSC4SimFireCrew_Vtable8,  addr::cSC4SimFireCrew_VtableC,
                                           addr::cSC4SimFireCrew_Vtable20, addr::cSC4SimFireCrew_Vtable38};

// 0x00544890, cdecl (size): a 0x270-byte block from the pedestrian pool
// (0x00B0A018, its page at +4: the free head at +8, the count at +0xC), a new
// page from SC4EdgePool::Grow when the page has none; other sizes from
// SC4::operator_new (a tail jump).
void* __cdecl PedestrianNew(std::size_t size)
{
    static bool logged;
    FirstCall("PedestrianNew", logged);
    if (size != kPedestrianSize)
        return CallCdecl<void*>(addr::SC4_operator_new, size);
    char* block = At<char*>(Global<char*>(addr::cSC4Pedestrian_Pool + 4), 8);
    if (!block)
        block = CallThis<char*>(addr::SC4EdgePool_Grow, reinterpret_cast<void*>(addr::cSC4Pedestrian_Pool));
    At<char*>(Global<char*>(addr::cSC4Pedestrian_Pool + 4), 8) = At<char*>(block, 0);
    ++At<std::uint32_t>(Global<char*>(addr::cSC4Pedestrian_Pool + 4), 0xC);
    return block;
}

// 0x00545410, on the pool (block), ret 4: the block back onto its page's free
// list (the page is the block's address & 0xFFFF0000); the page let go at
// count 0 (SC4Pool::ReleaseBlock) and reopened at 0x4000004E
// (SC4Pool::ReopenBlock).
void __fastcall PedestrianPoolFree(void* pool, void*, char* block)
{
    static bool logged;
    FirstCall("PedestrianPoolFree", logged);
    if (!block)
        return;
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(block) & 0xFFFF0000u);
    At<char*>(block, 0) = At<char*>(page, 8);
    At<char*>(page, 8) = block;
    const std::uint32_t count = --At<std::uint32_t>(page, 0xC);
    if (count == 0)
        CallThis<void>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (count == 0x4000004Eu)
        CallThis<void>(addr::SC4Pool_ReopenBlock, pool, page);
}

// 0x00545130, cdecl (block, size): nothing for null; a 0x270-byte block back
// to the pool, others to SC4::operator_delete.
void __cdecl PedestrianDelete(void* block, std::size_t size)
{
    static bool logged;
    FirstCall("PedestrianDelete", logged);
    if (!block)
        return;
    if (size == kPedestrianSize)
        CallThis<void>(addr::cSC4Pedestrian_PoolFree, reinterpret_cast<void*>(addr::cSC4Pedestrian_Pool), block);
    else
        CallCdecl<void>(addr::SC4_operator_delete, block);
}

// The GZCOM factories (cdecl): a block of the allocator, null when it gives
// none.

// 0x005149A0, the pedestrian's: the constructor with four zero words.
void* __cdecl PedestrianCreate()
{
    static bool logged;
    FirstCall("PedestrianCreate", logged);
    void* const block = CallCdecl<void*>(addr::cSC4Pedestrian_New, kPedestrianSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4Pedestrian_Construct, block, 0u, 0u, 0u, 0u);
}

// 0x005149D0, the one-shot's.
void* __cdecl OneShotCreate()
{
    static bool logged;
    FirstCall("OneShotCreate", logged);
    void* const block = CallCdecl<void*>(addr::cSC4Pedestrian_New, kOneShotSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4PedestrianOneShot_Construct, block, 0u, 0u, 0u, 0u);
}

// 0x005147A0, the riot police's: the constructor with no car.
void* __cdecl RiotPoliceCreate()
{
    static bool logged;
    FirstCall("RiotPoliceCreate", logged);
    void* const block = CallCdecl<void*>(addr::cSC4Pedestrian_New, kRiotPoliceSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4RiotPolice_Construct, block, static_cast<void*>(nullptr));
}

// 0x00514780, the fire crew's: the constructor with no truck.
void* __cdecl FireCrewCreate()
{
    static bool logged;
    FirstCall("FireCrewCreate", logged);
    void* const block = CallCdecl<void*>(addr::cSC4Pedestrian_New, kFireCrewSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4SimFireCrew_Construct, block, static_cast<void*>(nullptr));
}

// 0x00545F20, the pedestrian's constructor (four words for the core), ret
// 0x10: the automaton base's, the six vtables, +0x254 and the byte +0x258
// zero, the point three copies of the float 0x00B228DC, +0x268 zero, +0x26C
// 0.01, the occupant type; a time drawn from the generator 0x00B21D78
// (cRZRandomSuper::Between of 0x00B21BE0 and 0x00B21BE4, as a signed int,
// times 0x00A9A3FC) into +0x80 and, when it differs from +0x1A4 (or is
// unordered), the smoothed value at +0x1A0 (0x004FA960's answer, the time,
// 0, 0).
char* __fastcall PedestrianConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                     std::uint32_t d)
{
    static bool logged;
    FirstCall("PedestrianConstruct", logged);
    CallThis<char*>(addr::cSC4AutomatonBase_Construct, self, a, b, c, d);
    SetVtables(self, kPedestrianTables);
    At<std::uint32_t>(self, ped::kWord) = 0;
    At<std::uint8_t>(self, ped::kByte) = 0;
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, ped::kPoint + i) = Global<std::uint32_t>(addr::cSC4Pedestrian_DefaultFloat);
    At<std::uint32_t>(self, ped::kWord268) = 0;
    At<std::uint32_t>(self, ped::kFloat) = 0x3C23D70Au; // 0.01f
    At<std::uint32_t>(self, kType) = kPedestrianType;
    const std::uint32_t drawn =
        CallThis<std::uint32_t>(addr::cRZRandomSuper_Between, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random),
                                Global<std::uint32_t>(addr::cSC4Pedestrian_DrawLow),
                                Global<std::uint32_t>(addr::cSC4Pedestrian_DrawHigh));
    const float value = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(drawn)) *
                                           static_cast<double>(Global<float>(addr::cSC4Pedestrian_DrawScale)));
    At<float>(self, kDraw) = value;
    // FUCOMPP: equal skips; different or unordered sets the smoothed value.
    if (!(value == At<float>(self, kSmoothed + 4))) {
        const double now = CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, self + kSmoothed);
        At<float>(self, kSmoothed) = static_cast<float>(now);
        At<float>(self, kSmoothed + 4) = value;
        At<std::uint32_t>(self, kSmoothed + 8) = 0;
        At<std::uint32_t>(self, kSmoothed + 0xC) = 0;
    }
    return self;
}

// 0x00546860, the one-shot's constructor (the same four words), ret 0x10:
// the pedestrian's, the six vtables, the byte +0x270 5.
char* __fastcall OneShotConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                  std::uint32_t d)
{
    static bool logged;
    FirstCall("OneShotConstruct", logged);
    CallThis<char*>(addr::cSC4Pedestrian_Construct, self, a, b, c, d);
    SetVtables(self, kOneShotTables);
    At<std::uint8_t>(self, oneshot::kByte) = 5;
    return self;
}

// 0x00547800, the riot police's constructor (car), ret 4: the pedestrian's
// with four zero words, the six vtables, the car held at +0x270, +0x274 the
// word 0x00B228E0, the bytes +0x278 1, +0x279 and +0x27A zero.
char* __fastcall RiotPoliceConstruct(char* self, void*, void* car)
{
    static bool logged;
    FirstCall("RiotPoliceConstruct", logged);
    CallThis<char*>(addr::cSC4Pedestrian_Construct, self, 0u, 0u, 0u, 0u);
    SetVtables(self, kRiotTables);
    At<void*>(self, crew::kVehicle) = car;
    if (car)
        Virtual<std::uint32_t>(car, slot::kAddRef);
    At<std::uint8_t>(self, riot::kByte279) = 0;
    At<std::uint8_t>(self, riot::kByte27A) = 0;
    At<std::uint32_t>(self, crew::kWord) = Global<std::uint32_t>(addr::cSC4RiotPolice_DefaultWord);
    At<std::uint8_t>(self, crew::kByte) = 1;
    return self;
}

// A 12-byte occupant filter (SC4::OccupantFilterBase) with the two vtables,
// or null when SC4::operator_new gives none.
void* MakeFilter(std::uintptr_t table, std::uintptr_t table2)
{
    char* const filter = CallCdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (!filter)
        return nullptr;
    CallThis<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0) = table;
    At<std::uintptr_t>(filter, 4) = table2;
    return filter;
}

// 0x0054A2A0, the fire crew's constructor (truck), ret 4: the pedestrian's
// with four zero words, the six vtables, the truck held at +0x270, +0x274 the
// word 0x00B228EC, the byte +0x278 1, +0x27C to +0x2B0 zero (+0x2A4 a byte;
// +0x2B4 left), the point +0x2B8 three copies of the float 0x00B228F0; then
// the fire truck's two filters held into +0x298 and +0x29C.
char* __fastcall FireCrewConstruct(char* self, void*, void* truck)
{
    static bool logged;
    FirstCall("FireCrewConstruct", logged);
    CallThis<char*>(addr::cSC4Pedestrian_Construct, self, 0u, 0u, 0u, 0u);
    SetVtables(self, kFireTables);
    At<void*>(self, crew::kVehicle) = truck;
    if (truck)
        Virtual<std::uint32_t>(truck, slot::kAddRef);
    At<std::uint32_t>(self, crew::kWord) = Global<std::uint32_t>(addr::cSC4SimFireCrew_DefaultWord);
    At<std::uint8_t>(self, crew::kByte) = 1;
    for (std::size_t at = fire::kHeld27C; at <= fire::kWord2A0; at += 4)
        At<std::uint32_t>(self, at) = 0;
    At<std::uint8_t>(self, fire::kByte2A4) = 0;
    for (std::size_t at = fire::kPoint2A8; at < fire::kFloat2B4; at += 4)
        At<std::uint32_t>(self, at) = 0;
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, fire::kPoint2B8 + i) = Global<std::uint32_t>(addr::cSC4SimFireCrew_DefaultFloat);
    Assign(self, fire::kFilter, MakeFilter(addr::SC4OccupantFilter43_vftable, addr::SC4OccupantFilter43_vftable2));
    Assign(self, fire::kFilter2, MakeFilter(addr::cSC4FireTruck_FilterVtable, addr::cSC4FireTruck_FilterVtable2));
    return self;
}

// The QueryInterfaces (iid, out), ret 8: the class's IDs answer the object
// (AddRef, slot 1); the rest the automaton base's 0x0054D7F0 (a tail jump).
std::uint8_t QueryAs(char* self, std::uint32_t iid, void** out, bool oneShot)
{
    if (iid == kPedestrianClassId || (oneShot && iid == kOneShotClassId)) {
        *out = self;
        Virtual<std::uint32_t>(self, slot::kAddRef);
        return 1;
    }
    return CallThis<std::uint8_t>(addr::cSC4AutomatonBase_QueryInterface, self, iid, out);
}

// 0x00544750, the pedestrian's slot 0 (also the riot police's and the fire
// crew's, by a jump): 0x896E75AF.
std::uint8_t __fastcall PedestrianQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("PedestrianQueryInterface", logged);
    return QueryAs(self, iid, out, false);
}

// 0x00544780, the one-shot's slot 0: 0xAA09DC28 or 0x896E75AF.
std::uint8_t __fastcall OneShotQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("OneShotQueryInterface", logged);
    return QueryAs(self, iid, out, true);
}

// 0x005460F0, the pedestrian's deleting destructor (flags; +0xC slot 22
// through 0x005460C0), ret 4: the automaton base's body (inlined in the
// game), then with flag 1 a non-null block back to the pool.
char* __fastcall PedestrianDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("PedestrianDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4AutomatonBase_Destroy, self);
    if ((flags & 1) && self)
        CallThis<void>(addr::cSC4Pedestrian_PoolFree, reinterpret_cast<void*>(addr::cSC4Pedestrian_Pool), self);
    return self;
}

// 0x00546960, the one-shot's (+0xC slot 22 through 0x00546920), ret 4: the
// automaton base's body 0x00504E30, then with flag 1 a non-null block to
// SC4::operator_delete.
char* __fastcall OneShotDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("OneShotDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4AutomatonBase_Destroy, self);
    if ((flags & 1) && self)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x005478E0, the riot police's (+0xC slot 22 through 0x00547890), ret 4:
// the car +0x270 released (not cleared), the automaton base's body, then
// with flag 1 0x00545130 (this, 0x27C).
char* __fastcall RiotPoliceDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("RiotPoliceDeletingDestructor", logged);
    if (void* const car = At<void*>(self, crew::kVehicle))
        Release(car);
    CallThis<void*>(addr::cSC4AutomatonBase_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::cSC4Pedestrian_Delete, static_cast<void*>(self), kRiotPoliceSize);
    return self;
}

// 0x0054A530, the fire crew's destructor body: +0x29C, +0x298, +0x294 and
// +0x290 released, the block +0x284 freed (SC4::Free), +0x280, +0x27C and the
// truck +0x270 released (none cleared), then the automaton base's body
// (inlined in the game, ending in a tail jump to the core's destructor).
void* __fastcall FireCrewDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("FireCrewDestroy", logged);
    for (const std::size_t at : {fire::kFilter2, fire::kFilter, fire::kHeld294, fire::kHeld290})
        if (void* const held = At<void*>(self, at))
            Release(held);
    if (void* const block = At<void*>(self, fire::kBlock))
        CallCdecl<void>(addr::SC4_Free, block);
    for (const std::size_t at : {fire::kHeld280, fire::kHeld27C, crew::kVehicle})
        if (void* const held = At<void*>(self, at))
            Release(held);
    return CallThis<void*>(addr::cSC4AutomatonBase_Destroy, self);
}

// 0x0054A500, the fire crew's deleting destructor (+0xC slot 22 through
// 0x0054A470), ret 4: the body, then with flag 1 0x00545130 (this, 0x2C4).
char* __fastcall FireCrewDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("FireCrewDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4SimFireCrew_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::cSC4Pedestrian_Delete, static_cast<void*>(self), kFireCrewSize);
    return self;
}

// The serializable part's slot 5 of each class: its class ID.

// 0x00546030, the pedestrian's.
std::uint32_t __fastcall PedestrianGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("PedestrianGetGZCLSID", logged);
    return kPedestrianClassId;
}

// 0x005468C0, the one-shot's.
std::uint32_t __fastcall OneShotGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("OneShotGetGZCLSID", logged);
    return kOneShotClassId;
}

// 0x00547880, the riot police's.
std::uint32_t __fastcall RiotPoliceGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("RiotPoliceGetGZCLSID", logged);
    return kRiotPoliceClassId;
}

// 0x0054A430, the fire crew's.
std::uint32_t __fastcall FireCrewGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("FireCrewGetGZCLSID", logged);
    return kFireCrewClassId;
}

// ====================================================================================
// The save (this = the object + 8, the serializable part)

// 0x00544650, the pedestrian's +8 slot 3 (stream), ret 4: version 5
// (uint16), the automaton base's Write; when true the byte +0x258
// (SC4Stream::WriteBool), the point +0x25C, the float +0x26C and the word
// +0x254. Whether the stream's error is zero.
std::uint32_t __fastcall PedestrianWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("PedestrianWrite", logged);
    char* const self = part - kSerializable;
    StreamCall(s, stream::kUint16, static_cast<std::uint32_t>(kPedestrianVersion));
    if (CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Write, part, s)) {
        CallCdecl<void>(addr::SC4Stream_WriteBool, s, static_cast<std::uint32_t>(At<std::uint8_t>(self, ped::kByte)));
        CallCdecl<void>(addr::SC4Stream_WriteVector3, s, static_cast<void*>(self + ped::kPoint));
        StreamCall(s, stream::kFloat32, At<float>(self, ped::kFloat));
        StreamCall(s, stream::kUint32, At<std::uint32_t>(self, ped::kWord));
    }
    return StreamGood(s);
}

// 0x005446C0, the pedestrian's +8 slot 4 (stream), ret 4: 0 unless the
// version (into the stream argument's slot) is 5; the automaton base's Read;
// when true the byte, the point, the float and the word +0x254 (through a
// local holding the part's address). Whether the stream's error is zero.
std::uint32_t __fastcall PedestrianRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("PedestrianRead", logged);
    char* const self = part - kSerializable;
    if (VersionInArgument(s) != kPedestrianVersion)
        return 0;
    if (CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Read, part, s)) {
        CallCdecl<void>(addr::SC4Stream_ReadBool, s, static_cast<void*>(self + ped::kByte));
        CallCdecl<void>(addr::SC4Stream_ReadVector3, s, static_cast<void*>(self + ped::kPoint));
        StreamCall(s, stream::kFloat32, &At<float>(self, ped::kFloat));
        std::uint32_t word = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(part));
        StreamCall(s, stream::kUint32, &word);
        At<std::uint32_t>(self, ped::kWord) = word;
    }
    return StreamGood(s);
}

// The riot police's and the fire crew's shared part of the save: version 2,
// the pedestrian's Write; when true the vehicle +0x270 (a reference), the
// float +0x274 and the byte +0x278 (uint8).
bool CrewWriteHead(char* part, void* s)
{
    char* const self = part - kSerializable;
    StreamCall(s, stream::kUint16, static_cast<std::uint32_t>(kCrewVersion));
    if (!CallThis<std::uint8_t>(addr::cSC4Pedestrian_Write, part, s))
        return false;
    CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(self, crew::kVehicle));
    StreamCall(s, stream::kFloat32, At<float>(self, crew::kWord));
    StreamCall(s, stream::kUint8, static_cast<std::uint32_t>(At<std::uint8_t>(self, crew::kByte)));
    return true;
}

// And the Reads': the version into the argument's slot (0 when not 2), the
// pedestrian's Read; when true the vehicle read as 0xA9B40F05 into a zeroed
// local and held into +0x270 when it differs (the old one released), +0x274
// through a local the game leaves unset (ours zero) and the byte +0x278.
// -1 for the wrong version, else whether the rest is read.
int CrewReadHead(char* part, void* s)
{
    char* const self = part - kSerializable;
    if (VersionInArgument(s) != kCrewVersion)
        return -1;
    if (!CallThis<std::uint8_t>(addr::cSC4Pedestrian_Read, part, s))
        return 0;
    void* vehicle = nullptr;
    CallCdecl<void>(addr::SC4Stream_ReadReference, s, kVehicleIid, static_cast<void*>(&vehicle), 0u);
    Assign(self, crew::kVehicle, vehicle);
    std::uint32_t word = 0;
    StreamCall(s, stream::kFloat32, reinterpret_cast<float*>(&word));
    At<std::uint32_t>(self, crew::kWord) = word;
    StreamCall(s, stream::kUint8, &At<std::uint8_t>(self, crew::kByte));
    return 1;
}

// 0x00546DB0, the riot police's +8 slot 3: the shared part, then the byte
// +0x279 (SC4Stream::WriteBool). Whether the stream's error is zero.
std::uint32_t __fastcall RiotPoliceWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("RiotPoliceWrite", logged);
    if (CrewWriteHead(part, s))
        CallCdecl<void>(addr::SC4Stream_WriteBool, s,
                        static_cast<std::uint32_t>(At<std::uint8_t>(part - kSerializable, riot::kByte279)));
    return StreamGood(s);
}

// 0x00546E20, the riot police's +8 slot 4: the shared part, then the byte
// +0x279 (SC4Stream::ReadBool).
std::uint32_t __fastcall RiotPoliceRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("RiotPoliceRead", logged);
    const int head = CrewReadHead(part, s);
    if (head < 0)
        return 0;
    if (head)
        CallCdecl<void>(addr::SC4Stream_ReadBool, s, static_cast<void*>(part - kSerializable + riot::kByte279));
    return StreamGood(s);
}

// 0x00547BD0, the fire crew's +8 slot 3: the shared part, then the word
// +0x2A0 (sint32), the byte +0x2A4 (SC4Stream::WriteBool), the point +0x2A8,
// the float +0x2B4 and the point +0x2B8.
std::uint32_t __fastcall FireCrewWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("FireCrewWrite", logged);
    char* const self = part - kSerializable;
    if (CrewWriteHead(part, s)) {
        StreamCall(s, stream::kSint32, At<std::uint32_t>(self, fire::kWord2A0));
        CallCdecl<void>(addr::SC4Stream_WriteBool, s, static_cast<std::uint32_t>(At<std::uint8_t>(self, fire::kByte2A4)));
        CallCdecl<void>(addr::SC4Stream_WriteVector3, s, static_cast<void*>(self + fire::kPoint2A8));
        StreamCall(s, stream::kFloat32, At<float>(self, fire::kFloat2B4));
        CallCdecl<void>(addr::SC4Stream_WriteVector3, s, static_cast<void*>(self + fire::kPoint2B8));
    }
    return StreamGood(s);
}

// 0x00547C80, the fire crew's +8 slot 4: the shared part, then the same
// fields read in place.
std::uint32_t __fastcall FireCrewRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("FireCrewRead", logged);
    char* const self = part - kSerializable;
    const int head = CrewReadHead(part, s);
    if (head < 0)
        return 0;
    if (head) {
        StreamCall(s, stream::kSint32, &At<std::uint32_t>(self, fire::kWord2A0));
        CallCdecl<void>(addr::SC4Stream_ReadBool, s, static_cast<void*>(self + fire::kByte2A4));
        CallCdecl<void>(addr::SC4Stream_ReadVector3, s, static_cast<void*>(self + fire::kPoint2A8));
        StreamCall(s, stream::kFloat32, &At<float>(self, fire::kFloat2B4));
        CallCdecl<void>(addr::SC4Stream_ReadVector3, s, static_cast<void*>(self + fire::kPoint2B8));
    }
    return StreamGood(s);
}

// ====================================================================================
// The queries

// 0x00544530, the pedestrian's slot 16 (point, heading), ret 8: the core's
// slot 16 0x005080C0 with the heading's y zeroed (a local copy), or no
// heading.
void __fastcall PedestrianPlace(char* self, void*, const float* point, const float* heading)
{
    static bool logged;
    FirstCall("PedestrianPlace", logged);
    if (!heading) {
        CallThis<void>(addr::cSC4AutomatonCore_SetPlacement, self, point, static_cast<const float*>(nullptr));
        return;
    }
    float flat[3];
    std::memcpy(&flat[0], &heading[0], 4);
    flat[1] = 0.0f;
    std::memcpy(&flat[2], &heading[2], 4);
    CallThis<void>(addr::cSC4AutomatonCore_SetPlacement, self, point, static_cast<const float*>(flat));
}

// 0x00546040, the pedestrian's slot 17: the float 0.5 (0x00A84D2C) in ST0.
float __fastcall PedestrianSlot17(void*, void*)
{
    static bool logged;
    FirstCall("PedestrianSlot17", logged);
    return Global<float>(addr::SC4_FloatHalf);
}

// 0x00544D90, the pedestrian's slot 174: the city's slot 44 (a tail jump; no
// null test).
void* __fastcall PedestrianCityCrewService(void*, void*)
{
    static bool logged;
    FirstCall("PedestrianCityCrewService", logged);
    return Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
}

// 0x005468D0, the one-shot's slot 177: the byte +0x270 (AL only in the game).
std::uint32_t __fastcall OneShotByte(char* self, void*)
{
    static bool logged;
    FirstCall("OneShotByte", logged);
    return At<std::uint8_t>(self, oneshot::kByte);
}

// 0x005468E0, the one-shot's slot 178 (byte), ret 4: the byte +0x270.
void __fastcall OneShotSetByte(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("OneShotSetByte", logged);
    At<std::uint8_t>(self, oneshot::kByte) = static_cast<std::uint8_t>(value);
}

// 0x00546CB0, the crews' word (const word*), ret 4: +0x274. The fire truck
// and the patrol car call it at its address.
void __fastcall CrewSetWord(char* self, void*, const std::uint32_t* word)
{
    static bool logged;
    FirstCall("CrewSetWord", logged);
    At<std::uint32_t>(self, crew::kWord) = *word;
}

// ====================================================================================
// Installation

bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_AUTOMATA_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("automata pedestrians %s: no address entry for 0x%08X", what,
                       static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("automata pedestrians %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Part ped1.
const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Create, PedestrianCreate),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_Create, OneShotCreate),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Create, RiotPoliceCreate),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_Create, FireCrewCreate),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_New, PedestrianNew),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Delete, PedestrianDelete),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_PoolFree, PedestrianPoolFree),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Construct, PedestrianConstruct),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_Construct, OneShotConstruct),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Construct, RiotPoliceConstruct),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_Construct, FireCrewConstruct),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_QueryInterface, PedestrianQueryInterface),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_QueryInterface, OneShotQueryInterface),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_DeletingDestructor, PedestrianDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_DeletingDestructor, OneShotDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_DeletingDestructor, RiotPoliceDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_Destroy, FireCrewDestroy),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_DeletingDestructor, FireCrewDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_GetGZCLSID, PedestrianGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_GetGZCLSID, OneShotGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_GetGZCLSID, RiotPoliceGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_GetGZCLSID, FireCrewGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Write, PedestrianWrite),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Read, PedestrianRead),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Write, RiotPoliceWrite),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Read, RiotPoliceRead),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_Write, FireCrewWrite),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_Read, FireCrewRead),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Place, PedestrianPlace),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Slot17, PedestrianSlot17),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_CityCrewService, PedestrianCityCrewService),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_Byte, OneShotByte),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_SetByte, OneShotSetByte),
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_SetWord274, CrewSetWord),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode) { return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.pedestrians", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Pedestrian_Construct,
                        "Our pedestrians (the pedestrian, the one-shot, the riot police and the fire crew), the "
                        "objects: the four factories, the allocator and its release, the pool's free, the four "
                        "constructors, the two QueryInterfaces, the four deleting destructors, the fire crew's "
                        "destructor body and the four GetGZCLSIDs; the pedestrian's, the riot police's and the fire "
                        "crew's Writes and Reads; the pedestrian's slots 16, 17 and 174, the one-shot's 177 and 178 "
                        "and the crews' word (34 detours, part ped1; src/core/automata_pedestrians.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_pedestrians
