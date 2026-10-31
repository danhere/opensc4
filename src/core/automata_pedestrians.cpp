// Our pedestrians, part ped1 (build 641): cSC4Pedestrian on the automaton base
// (src/core/automata_vehicle.cpp) and cSC4PedestrianOneShot, cSC4RiotPolice
// and cSC4SimFireCrew on it. ped1: the objects (the four factories, the
// allocator and its release, the pool's free, the four constructors, the two
// QueryInterfaces, the four deleting destructors and the fire crew's
// destructor body, the four GetGZCLSIDs), the save (the pedestrian's, the
// riot police's and the fire crew's Writes and Reads) and the small queries
// (the pedestrian's slots 16, 17 and 174, the one-shot's 177 and 178 and the
// crews' word +0x274). ped2a: the pedestrian's states and frame (its Init,
// slot 72, the animation 103, the states 109 to 111, slots 135, 155, 158,
// 165 and 176, and the frames of the states 4 and 0xC). ped2b: the walks
// (the frames of the states 2, 3, 6, 8 and 9, the bump 0x00545840 and the
// step 0x00545D60). ped3: the one-shot's Init and frame and the riot
// police's Init, Shutdown, frame, slot 74, message helper 0x00546D20, dispatch
// 0x00546F30 and its clamp 0x00547930. ped4a: the fire crew's dispatch
// 0x00547EF0. The layout is in
// src/core/automata_pedestrians.h and the survey in docs/automata.md, "Part
// ped". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.pedestrians: the 34 detours in kObject (ped1), the
//   13 in kStates (ped2a), the 7 in kWalks (ped2b), the 9 in kCrews (ped3)
//   and the 1 in kFireDispatch (ped4a) below.
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
// ped2a's oddities kept: slot 155 takes the y difference as the position's y
// less itself and rounds the x difference to a float, while the state 4's
// frame counts the y difference in the length (then sets y to 0) and keeps x
// unrounded; slot 165 normalises +0xA4 without a zero test; the state 0xC's
// frame null-tests neither the simulator nor the interface it asks for (and
// holds and lets go of it once more around nothing); slot 135 answers the
// state's type in the states 8 and 9; slot 103 sets +0x26C to 0.01 on every
// frame it animates and does not null-test the first model. Stack garbage
// the game passes on (ours zeros): slot 165's side byte (three bytes above
// it) to the network part's slot 5, and its key when the part finds no path
// (unread). AL only in the game: slots 72, 109 and 165 where they answer the
// base's; ours zero-extends.
//
// Floats (53 bits): slot 103's timer is seconds + timer stored as a float;
// slot 165 keeps 1 / length unrounded; slot 155's and the state 4's factor is
// stored as a float.
//
// ped2b's oddities kept: the bump compares its timer unrounded with 500
// after storing it as a float, calls the occupant manager's slot 48 on the
// list's second stack word (the manager, as the game leaves it there) and
// does not read slot 37's answer; it pushes the other pedestrian through its
// occupant part (+0x254 and +0x258 there are the pedestrian's +0x258 and
// +0x25C). The step reads the network map before the speed and gives slot 16
// the direction as passed, not its scaled copy; its EAX is 1 when it placed
// (not bumped). The state 3's frame tests neither the first model past the
// list nor the names' count but unsigned; the state 6's tests neither the
// current state nor the object's point (into the game's stack unset; ours
// zeros), passes the side to the map's slot 15 with three bytes of the stack
// above it (ours zeros), and packs the key with the groups' two low bytes
// swapped; its loop never ends for a side byte above 3 unless a path is
// found. The states 8's and 9's frames do not null-test the current state;
// a missing cell occupant removes the pedestrian (slot 13 with 0) without
// slot 105; starting the state 8 returns before slot 105 too. The smoothed
// speed's target is compared by its bits (the states 8 and 9). AL only in
// the game: the frames' and the bump's AL 0; ours zero-extends.
//
// Floats (53 bits, ped2b): the cell is the coordinate times 0.0625 truncated
// (__ftol2, 64-bit, its low word). The step and the frames keep each
// factor (1 / length, draw / length, step / length) unrounded but where the
// game stores it as a float (the state 2's y and z, the state 3's push
// scale is unrounded); the sums with z are left unrounded before the
// normalising length in the states 8 and 9 and the bump's differences are
// squared unrounded and stored as floats.
//
// ped3's oddities kept: the one-shot's Init null-tests the property but not
// its value, and gives SC4Random::Below high - low + 1 as it comes (zero or
// less when high is below low); its frame counts down only by the first
// model that is not null and goes on through the list after removing. The
// riot police's code reads the car (+0x270) without a null test (the frame's
// later test for the message is dead); the Init counts the level +0x278 up
// while +0x274 is below or unordered with the bound (a NaN gives 3), at 3
// takes the fourth word 0 of 0x00A9AE28 (no start model, so no manager
// look-up) and drops the sound the city's sounds answer; at level 3 the
// messages take the fourth word of 0x00A9AE80, the next datum's (the float
// 1e-4). The message helper and the clamp do not read their this; the
// helper calls through a failed allocation. The dispatch holds the map but
// calls its slot 18 without a null test; on the mission 5 it normalises the
// way to the car's point only when it is shorter than 1e-5 and else takes
// the car's heading; on the mission 1 the side is (m + c) x 0 - c, so an
// infinity or NaN in the car's transform survives the 0, and the target's
// cell comes from locals the city's slot 95 may leave unset (ours zero).
// AL only in the game: the one-shot's Init and frame and the riot police's
// Init's 1 and frame; ours zero-extends.
//
// Floats (53 bits, ped3): FSIN and FCOS of the dispatch's quarter turn at
// 64 bits (x87 code), each product with them rounded to 53; the mission 5's
// x difference a float, y and z not, 1 / length unrounded; the targets' x
// sums unrounded where the game keeps them in a register (y and z, and the
// mission 1's way back x and y, as floats); the clamp's bounds the cells as
// floats less 1.
//
// Still the game's, at their addresses: the automaton base's code (part vb),
// the riot police's and the fire crew's jumps to the pedestrian's
// QueryInterface and the adjustor thunks the vtables hold, the occupant
// filters' code, the stream helpers, the pool's growth and release, the
// groups' split and pack (0x0053D100, 0x004ADE80), the jump 0x00544520 to
// the base's Shutdown, the cGZMessage and string code, and the fire crew's
// states, frames, Init and Shutdown (part ped4).

#include "core/automata_pedestrians.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
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
// The states and the frame (ped2a)

bool CheckFlag(char* self, std::uint32_t flag) { return Virtual<std::uint8_t>(self, pslot::kCheckFlag, flag) != 0; }

void SetFlag(char* self, std::uint32_t flag, std::uint32_t on) { Virtual<std::uint32_t>(self, pslot::kSetFlag, flag, on); }

// A string of the game's (cRZBaseString's layout).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t word10;
};

// Slot 21 given a string of the text 0x00B0A008 points to ("idle"), made by
// `fromText` (SC4String::FromText) or else over the text's length
// (SC4String::FromRange); the buffer freed after.
void SayIdle(char* self, bool fromText)
{
    const char* const text = Global<const char*>(addr::cSC4Pedestrian_IdleName);
    GameString name{};
    if (fromText) {
        CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&name), text);
    } else {
        char tag = 0;
        CallThis<void>(addr::SC4String_FromRange, static_cast<void*>(&name.begin), text, text + std::strlen(text),
                       static_cast<void*>(&tag));
        name.vtable = addr::SC4String_Vtable;
        name.word10 = 0;
    }
    Virtual<std::uint32_t>(self, pslot::kSlot21, static_cast<void*>(&name));
    name.vtable = addr::SC4String_Vtable;
    if (name.begin)
        CallCdecl<void>(addr::SC4_Free, static_cast<void*>(name.begin));
}

// 0x005444F0, the pedestrian's slot 3: slot 158 unless the occupant part
// (+4) says the object is initialised (its slot 5); then the automaton base's
// Init 0x0054D790 (a tail jump), its EAX.
std::uint32_t __fastcall PedestrianInit(char* self, void*)
{
    static bool logged;
    FirstCall("PedestrianInit", logged);
    if (!Virtual<std::uint8_t>(self + 4, pslot::kIsInitialised))
        Virtual<std::uint32_t>(self, pslot::kSlot158);
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_Init, self);
}

// 0x00544580, slot 72 (word, seconds), ret 8: the automaton base's frame
// (0x0054E050) between two looks at the automata flag 0x20; with the flag 2
// (slot 86), the flag newly on gives slot 176 and on before slot 19 (0). The
// base frame's AL (AL only in the game; ours zero-extends).
std::uint32_t __fastcall PedestrianFrame(char* self, void*, std::uint32_t word, float seconds)
{
    static bool logged;
    FirstCall("PedestrianFrame", logged);
    const bool before = CheckFlag(self, 0x20u);
    const std::uint8_t moved = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Frame, self, word, seconds);
    if (Virtual<std::uint8_t>(self, pslot::kFlag2)) {
        if (CheckFlag(self, 0x20u) && !before)
            Virtual<std::uint32_t>(self, pslot::kIdle);
        else if (before)
            Virtual<std::uint32_t>(self, pslot::kSwapStartModel, 0u);
    }
    return moved;
}

// 0x00544C90, slot 103 (seconds), ret 4: the animation. The timer +0x1C0
// gains the seconds first; AL 1 at once without the automata flags 0x2000 and
// 2 and with 0x20, and while the simulator 0x00B43D5C's slot 15 answers 1
// and its slot 17 0. Else +0x26C set to 0.01, each model given +0x26C (slot
// 14) and advanced (slot 15); with the list +0x188 not empty and models, the
// first model (not null-tested) at its last frame (slot 11 against slot 12
// less 1, signed) gives slot 102 (1, 0). AL 1.
std::uint32_t __fastcall PedestrianAnimate(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianAnimate", logged);
    At<float>(self, base::kTimer1C0) =
        static_cast<float>(static_cast<double>(seconds) + static_cast<double>(At<float>(self, base::kTimer1C0)));
    if (!CheckFlag(self, 0x2000u) && !CheckFlag(self, 2u) && CheckFlag(self, 0x20u))
        return 1;
    if (Virtual<std::uint8_t>(Global<void*>(addr::SC4_SimulatorService), pslot::kSimulator3C) &&
        !Virtual<std::uint8_t>(Global<void*>(addr::SC4_SimulatorService), pslot::kSimulator44))
        return 1;
    At<std::uint32_t>(self, ped::kFloat) = 0x3C23D70Au; // 0.01f
    for (char** it = At<char**>(self, base::kModels); it != At<char**>(self, base::kModelsEnd); ++it)
        if (char* const model = *it) {
            Virtual<std::uint32_t>(model, pslot::kModelSlot14, At<std::uint32_t>(self, ped::kFloat));
            Virtual<std::uint32_t>(model, pslot::kModelAdvance, seconds);
        }
    char* const list = At<char*>(self, base::kList188);
    if (At<char*>(list, 0) != list && At<char**>(self, base::kModels) != At<char**>(self, base::kModelsEnd)) {
        char* const first = *At<char**>(self, base::kModels);
        const std::int32_t last = Virtual<std::int32_t>(first, pslot::kModelFrames) - 1;
        if (Virtual<std::int32_t>(first, pslot::kModelFrame) >= last)
            Virtual<std::uint32_t>(self, pslot::kSlot102, 1u, 0u);
    }
    return 1;
}

// 0x00546BC0, slot 109 (seconds), ret 4: AL 0 with no states (+0xB4 =
// +0xB8); else the automaton base's (0x00550090), and when it answers AL 0 the
// current state's (slot 56) frame by its type: 2 0x00546150, 3 0x00545B10, 4
// 0x00545690, 6 0x00544950, 8 0x00546220 (seconds, 0), 9 0x00546530, 0xC
// 0x00545280 (their AL); other types and none AL 0.
std::uint32_t __fastcall PedestrianStateFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianStateFrame", logged);
    if (At<char*>(self, base::kStates) == At<char*>(self, base::kStatesEnd))
        return 0;
    const std::uint8_t done = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_StateFrame, self, seconds);
    if (done)
        return done;
    char* const current = Virtual<char*>(self, pslot::kCurrentState);
    if (!current)
        return 0;
    switch (At<std::uint32_t>(current, state::kType)) {
    case 2:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_State2Frame, self, seconds);
    case 3:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_State3Frame, self, seconds);
    case 4:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_State4Frame, self, seconds);
    case 6:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_State6Frame, self, seconds);
    case 8:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_State8Frame, self, seconds, 0u);
    case 9:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_State9Frame, self, seconds);
    case state::kBurn:
        return CallThis<std::uint8_t>(addr::cSC4Pedestrian_StateCFrame, self, seconds);
    default:
        return 0;
    }
}

// The smoothed speed (+0x1A0) towards 1/720 at once (SetSmoothedTarget,
// duration 0).
void SmoothToWalk(char* self)
{
    CallThis<void>(addr::cSC4AutomatonCore_SetSmoothedTarget, static_cast<void*>(self + base::kSmoothed), kSmoothTarget,
                   0u);
}

// 0x00546A10, slot 110 (state), ret 4: the automaton base's (0x005546E0);
// when it answers AL 0, the automata flags (slot 30) by the state's type: 1
// 0x2000, 0x4000 and 0x20000 on; 2 and 3 0x2000 on, 0x4000 off; 4 the same
// after 2 on; 6 2 on, 0x2000 off, slot 134; 7 nothing; 8 and 9 the smoothed
// speed to 1/720, then as 4, and 8 also 0x10000, 0x1000000, 0x2000000 and
// 0x10000000 on and 0x00546220 (0, 1); other types 0x4000 and 0x2000 off and
// slot 134; no state 0x2000 and 0x4000 off. AL 1.
std::uint32_t __fastcall PedestrianEnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("PedestrianEnterState", logged);
    if (CallThis<std::uint8_t>(addr::cSC4AutomatonBase_EnterState, self, entered))
        return 1;
    if (!entered) {
        SetFlag(self, 0x2000u, 0u);
        SetFlag(self, 0x4000u, 0u);
        return 1;
    }
    switch (At<std::uint32_t>(entered, state::kType)) {
    case 1:
        SetFlag(self, 0x2000u, 1u);
        SetFlag(self, 0x4000u, 1u);
        SetFlag(self, 0x20000u, 1u);
        break;
    case 2:
    case 3:
        SetFlag(self, 0x2000u, 1u);
        SetFlag(self, 0x4000u, 0u);
        break;
    case 4:
        SetFlag(self, 2u, 1u);
        SetFlag(self, 0x2000u, 1u);
        SetFlag(self, 0x4000u, 0u);
        break;
    case 6:
        SetFlag(self, 2u, 1u);
        SetFlag(self, 0x2000u, 0u);
        Virtual<std::uint32_t>(self, pslot::kPlaceOnNetwork);
        break;
    case 7:
        break;
    case 8:
        SmoothToWalk(self);
        SetFlag(self, 2u, 1u);
        SetFlag(self, 0x2000u, 1u);
        SetFlag(self, 0x4000u, 0u);
        SetFlag(self, 0x10000u, 1u);
        SetFlag(self, 0x1000000u, 1u);
        SetFlag(self, 0x2000000u, 1u);
        SetFlag(self, 0x10000000u, 1u);
        CallThis<std::uint32_t>(addr::cSC4Pedestrian_State8Frame, self, 0.0f, 1u);
        break;
    case 9:
        SmoothToWalk(self);
        SetFlag(self, 2u, 1u);
        SetFlag(self, 0x2000u, 1u);
        SetFlag(self, 0x4000u, 0u);
        break;
    default:
        SetFlag(self, 0x4000u, 0u);
        SetFlag(self, 0x2000u, 0u);
        Virtual<std::uint32_t>(self, pslot::kPlaceOnNetwork);
        break;
    }
    return 1;
}

// 0x005445F0, slot 111 (state), ret 4: leaving the state 8 turns the automata
// flag 0x10000 off; then the automaton base's (0x005535D0), its EAX.
std::uint32_t __fastcall PedestrianLeaveState(char* self, void*, char* left)
{
    static bool logged;
    FirstCall("PedestrianLeaveState", logged);
    if (left && At<std::uint32_t>(left, state::kType) == 8)
        SetFlag(self, 0x10000u, 0u);
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_LeaveState, self, left);
}

// 0x00544C40, slot 135: the occupant +0x1EC let go (cleared, then released);
// in the state 8 or 9 (slot 56) nothing more (EAX the type); else the
// automaton base's RemoveNow 0x0054D820 (a tail jump), its EAX.
std::uint32_t __fastcall PedestrianRemove(char* self, void*)
{
    static bool logged;
    FirstCall("PedestrianRemove", logged);
    if (void* const occupant = At<void*>(self, base::kOccupant)) {
        At<void*>(self, base::kOccupant) = nullptr;
        Release(occupant);
    }
    if (char* const current = Virtual<char*>(self, pslot::kCurrentState)) {
        const std::uint32_t type = At<std::uint32_t>(current, state::kType);
        if (type == 8 || type == 9)
            return type;
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_RemoveNow, self);
}

// 0x00545360, slot 155 (point, out), ret 8: the unit vector from the
// position to the point in x and z: (0, 0, 1) when the distance is at most
// 1e-5 (0x00A9BCD8). The x difference is rounded to a float; the y
// difference is the position's y less itself (0 but for infinities and NaN);
// the length squares the unrounded z difference times its float copy; the
// factor 1 / length (0x00B0A00C over it) is stored as a float.
void __fastcall PedestrianDirectionTo(char* self, void*, const float* point, float* out)
{
    static bool logged;
    FirstCall("PedestrianDirectionTo", logged);
    const float* const position = &At<float>(self, base::kPosition);
    const float dx = static_cast<float>(static_cast<double>(point[0]) - position[0]);
    const float dy = static_cast<float>(static_cast<double>(position[1]) - position[1]);
    const double dz = static_cast<double>(point[2]) - position[2];
    const float dzf = static_cast<float>(dz);
    const double length =
        std::sqrt((dz * dzf + static_cast<double>(dy) * dy) + static_cast<double>(dx) * static_cast<double>(dx));
    const double epsilon = Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon);
    if (length < epsilon || length == epsilon) {
        out[1] = 0.0f;
        out[0] = 0.0f;
        out[2] = 1.0f;
        return;
    }
    const float factor = static_cast<float>(static_cast<double>(Global<float>(addr::cSC4Pedestrian_One)) / length);
    out[0] = static_cast<float>(static_cast<double>(dx) * factor);
    out[1] = static_cast<float>(static_cast<double>(dy) * factor);
    out[2] = static_cast<float>(static_cast<double>(dzf) * factor);
}

// 0x00544620, slot 158: +0x200 a float from the automata's generator between
// the floats 0x00B21CE8 and 0x00B21CEC (cRZRandom::Range, as doubles).
void __fastcall PedestrianDrawFloat200(char* self, void*)
{
    static bool logged;
    FirstCall("PedestrianDrawFloat200", logged);
    const double high = Global<float>(addr::cSC4Pedestrian_Float200High);
    const double low = Global<float>(addr::cSC4Pedestrian_Float200Low);
    At<float>(self, base::kFloat200) = static_cast<float>(
        CallThis<double>(addr::cRZRandom_Range, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random), low, high));
}

// 0x00545160, slot 165: the automaton base's (0x00553AA0); when it answers AL
// 1 with +0x98, an occupant (+0x1EC) and the speed +0xA0 at least 20
// (0x00A9A3F4, not unordered): +0xA4 normalised in place (1 over the length
// kept at 53 bits, no zero test) and its best side found (slot 171: the
// occupant, checked); when that differs from +0x1F4's low byte (slot 136),
// the occupant's network part's path from the position (its slot 5: 2, 0xFF,
// the side, the position, the key out, 0) and with one slot 131 (the
// occupant, the key, 1), its EAX. Else the base's AL (AL only in the game).
std::uint32_t __fastcall PedestrianNextStep(char* self, void*)
{
    static bool logged;
    FirstCall("PedestrianNextStep", logged);
    const std::uint8_t stepped = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_NextStep, self);
    if (!stepped || !At<void*>(self, base::k98) || !At<void*>(self, base::kOccupant))
        return stepped;
    if (!(std::fabs(static_cast<double>(At<float>(self, base::kFloatA0))) >=
          static_cast<double>(Global<float>(addr::cSC4Pedestrian_TurnSpeed))))
        return stepped;
    float* const v = &At<float>(self, base::kVectorA4);
    const double x = v[0], y = v[1], z = v[2];
    const double inverse = static_cast<double>(Global<float>(addr::cSC4Pedestrian_One)) / std::sqrt((x * x + y * y) + z * z);
    v[0] = static_cast<float>(inverse * v[0]);
    v[1] = static_cast<float>(inverse * v[1]);
    v[2] = static_cast<float>(inverse * v[2]);
    // The side is a byte with three bytes of the game's stack above it, and
    // the key a local the part's slot 5 writes when it finds a path; ours 0.
    std::uint32_t side = 0;
    Virtual<std::uint32_t>(self, pslot::kBestDirection, At<void*>(self, base::kOccupant), static_cast<const float*>(v),
                           reinterpret_cast<std::uint8_t*>(&side), 1u);
    if ((Virtual<std::uint32_t>(self, pslot::kGroups) & 0xFFu) == (side & 0xFFu))
        return stepped;
    void* const part = Virtual<void*>(At<void*>(self, base::kOccupant), pslot::kNetworkPart);
    if (!part)
        return stepped;
    std::uint32_t key = 0;
    if (!Virtual<void*>(part, pslot::kFindPath, 2u, 0xFFu, side,
                        static_cast<const float*>(&At<float>(self, base::kPosition)), &key, 0u))
        return stepped;
    return Virtual<std::uint32_t>(self, pslot::kPathTo, At<void*>(self, base::kOccupant), key, 1u);
}

// 0x005457C0, slot 176: slot 21 with "idle" (SC4String::FromRange). AL 1.
std::uint32_t __fastcall PedestrianIdle(char* self, void*)
{
    static bool logged;
    FirstCall("PedestrianIdle", logged);
    SayIdle(self, false);
    return 1;
}

// 0x00545690, the state 4's frame (seconds), ret 4: without a current state
// (slot 56) holding an object (+8), slot 53. Else the object's slot 28 (this,
// the type, seconds) and the move to the destination (0x00550190); when the
// automata flag 0x20 is then on, the heading (slot 14) towards the object's
// point (its slot 11 into three copies of the float 0x00B228DC) in x and z
// (the length counts the unrounded y difference and squares the unrounded z
// difference times its float copy; x unrounded and z's float copy times the
// factor 1 / length stored as a float) and slot 21 with "idle"
// (SC4String::FromText). Then slot 105. AL 1.
std::uint32_t __fastcall PedestrianState4Frame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianState4Frame", logged);
    char* const current = Virtual<char*>(self, pslot::kCurrentState);
    if (!current || !At<void*>(current, state::kObject)) {
        Virtual<std::uint32_t>(self, pslot::kPopState);
        return 1;
    }
    Virtual<std::uint32_t>(At<void*>(current, state::kObject), pslot::kStateObjectFrame, static_cast<void*>(self),
                           At<std::uint32_t>(current, state::kType), seconds);
    CallThis<std::uint32_t>(addr::cSC4AutomatonBase_MoveToDestination, self, seconds);
    if (CheckFlag(self, 0x20u)) {
        std::uint32_t point[3];
        for (std::uint32_t& w : point)
            w = Global<std::uint32_t>(addr::cSC4Pedestrian_DefaultFloat);
        Virtual<std::uint32_t>(At<void*>(current, state::kObject), pslot::kStateObjectPoint, static_cast<void*>(point));
        float to[3];
        std::memcpy(to, point, sizeof to);
        const float* const position = &At<float>(self, base::kPosition);
        const double dx = static_cast<double>(to[0]) - position[0];
        const double dy = static_cast<double>(to[1]) - position[1];
        const double dz = static_cast<double>(to[2]) - position[2];
        const float dzf = static_cast<float>(dz);
        const double length = std::sqrt((dz * dzf + dy * dy) + dx * dx);
        const float factor = static_cast<float>(static_cast<double>(Global<float>(addr::cSC4Pedestrian_One)) / length);
        float heading[3];
        heading[1] = 0.0f;
        heading[0] = static_cast<float>(dx * factor);
        heading[2] = static_cast<float>(static_cast<double>(dzf) * factor);
        Virtual<std::uint32_t>(self, pslot::kSetHeading, static_cast<const float*>(heading));
        SayIdle(self, true);
    }
    Virtual<std::uint32_t>(self, pslot::kSlot105);
    return 1;
}

// 0x00545280, the state 0xC's frame (seconds), ret 4: down the state stack
// from its top, the first state 0xC whose object (+8) answers a word (its
// slot 6, not zero); with one, the city's simulator 0x621CDA33 (the city's
// slot 72, then the simulators' slot 7, not null-tested) as 0x49B43F57 (the
// answer not tested; the interface held and let go once more) told the word
// (slot 4) and 2 (slot 9), then released. Then the state set to 1 (slot 51:
// 1, 0, -1.0, 0, 0, 0, 0). AL 1.
std::uint32_t __fastcall PedestrianStateCFrame(char* self, void*, float)
{
    static bool logged;
    FirstCall("PedestrianStateCFrame", logged);
    std::uint32_t word = 0;
    const std::int32_t count =
        static_cast<std::int32_t>(At<char*>(self, base::kStatesEnd) - At<char*>(self, base::kStates)) >> 2;
    for (std::int32_t i = count - 1; i >= 0; --i) {
        char* const s = At<char**>(self, base::kStates)[i];
        if (At<std::uint32_t>(s, state::kType) != state::kBurn)
            continue;
        void* const object = At<void*>(s, state::kObject);
        if (!object)
            continue;
        word = Virtual<std::uint32_t>(object, pslot::kStateObjectWord);
        if (word)
            break;
    }
    if (word) {
        if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), pslot::kCitySimulators)) {
            void* const simulator = Virtual<void*>(simulators, pslot::kSimulatorById, kBurnSimulator);
            void* burn = nullptr;
            Virtual<std::uint8_t>(simulator, slot::kQueryInterface, kBurnInterface, static_cast<void*>(&burn));
            void* const held = burn;
            if (burn) {
                Virtual<std::uint32_t>(burn, slot::kAddRef);
                if (burn)
                    Release(burn);
            }
            Virtual<std::uint32_t>(held, pslot::kBurnSet, word);
            Virtual<std::uint32_t>(held, pslot::kBurnMode, 2u);
            Release(held);
        }
    }
    Virtual<std::uint32_t>(self, pslot::kSetState, 1u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
    return 1;
}

// ====================================================================================
// The walks (ped2b)

double Zero() { return Global<float>(addr::SC4_FloatZero); }
double One() { return Global<float>(addr::cSC4Pedestrian_One); }
void* Random() { return reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random); }

// cRZRandom::Range on the automata's generator.
double Draw(double low, double high) { return CallThis<double>(addr::cRZRandom_Range, Random(), low, high); }

// __ftol2's low word: truncation toward zero; what does not fit the 64-bit
// integer (and a NaN) is the x87's 0x8000000000000000, low half 0.
std::int32_t Ftol2(double value)
{
    if (!(std::fabs(value) < 9223372036854775808.0))
        return 0;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value));
}

// The cell (1/16) of a coordinate: the float times 0.0625, truncated.
std::int32_t CellOf(float coordinate)
{
    return Ftol2(static_cast<double>(coordinate) * Global<float>(addr::cSC4Pedestrian_CellScale));
}

void* Network() { return Virtual<void*>(Global<void*>(addr::SC4_City), wslot::kCityNetwork); }

// The smoothed speed (+0x1A0) towards the float `target` at once when its
// target's bits differ (an integer compare): the value now as a float, the
// target, and two zero words.
void SmoothTowards(char* self, std::uint32_t target)
{
    char* const smoothed = self + base::kSmoothed;
    if (At<std::uint32_t>(smoothed, 4) == target)
        return;
    At<float>(smoothed, 0) =
        static_cast<float>(CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, static_cast<void*>(smoothed)));
    At<std::uint32_t>(smoothed, 4) = target;
    At<std::uint32_t>(smoothed, 8) = 0;
    At<std::uint32_t>(smoothed, 0xC) = 0;
}

// The list node allocator 0x00B207E4 as the game inlines its fetch: when the
// cached pointer is null, the framework's slot 5 into a local, the local
// cached and then let go (SC4::ReleaseRef; the cache keeps no reference).
void* NodeAllocator()
{
    void*& cached = *reinterpret_cast<void**>(addr::SC4List_NodeAllocator2);
    if (!cached) {
        void* object = nullptr;
        if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
            Virtual<std::uint8_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, static_cast<void*>(&object));
        cached = object;
        CallThis<void>(addr::SC4_ReleaseRef, static_cast<void*>(&object));
    }
    return cached;
}

// A list of that allocator as the game keeps it on the stack: the sentinel
// and the next stack word (here the occupant manager, as the game's).
struct NodeList {
    char* head;
    void* word;
};

// 0x00545840, the bump (point, seconds), ret 8: +0x268 gains the seconds
// (stored as a float, the unrounded sum compared); below 500 (0x00A9A3F8,
// or unordered) AL 0. Else +0x268 zeroed; a list (a sentinel from the
// allocator) filled by the occupant manager 0x00B43D0C's slot 37 (answer not
// read) with up to 10 occupants of the pedestrian type in the box of the
// point 1 (0x00A81228) out in x and z, the point's y to 2 (0x00A8825C) up;
// down the list until one is pushed, each other than our occupant part (slot
// 5): its point (slot 8 into three 0x00B228DC) less ours (unrounded, then
// stored as floats); at a length below 1 it is pushed: its occupant part's
// byte +0x254 1 and point +0x258 the difference, or within 0.01 (0x00A9A3F0)
// two draws of -0.5 to 0.5 in x and z (y 0). The manager's slot 48 (on the
// list's second word once the loop ran, as the game reloads it), the list
// emptied (SC4List::Clear2) and its sentinel freed (the allocator's slot 4,
// fetched again). AL whether one was pushed.
std::uint32_t __fastcall PedestrianBump(char* self, void*, const float* point, float seconds)
{
    static bool logged;
    FirstCall("PedestrianBump", logged);
    const double timer = static_cast<double>(seconds) + At<float>(self, ped::kWord268);
    At<float>(self, ped::kWord268) = static_cast<float>(timer);
    if (!(timer >= Global<float>(addr::cSC4Pedestrian_BumpInterval)))
        return 0;
    At<std::uint32_t>(self, ped::kWord268) = 0;
    char* const head = Virtual<char*>(NodeAllocator(), 0x0C, 0xCu);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    float low[3], high[3];
    low[0] = static_cast<float>(static_cast<double>(point[0]) - one);
    low[1] = point[1];
    low[2] = static_cast<float>(static_cast<double>(point[2]) - one);
    high[0] = static_cast<float>(static_cast<double>(point[0]) + one);
    high[1] = static_cast<float>(static_cast<double>(point[1]) + Global<float>(addr::cSC4Vehicle_Two));
    high[2] = static_cast<float>(static_cast<double>(point[2]) + one);
    void* const mine = Virtual<void*>(self, wslot::kAsOccupant);
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    NodeList list{head, manager};
    Virtual<std::uint8_t>(manager, wslot::kOccupantsInBox, static_cast<void*>(&list), static_cast<float*>(low),
                          static_cast<float*>(high), kPedestrianType, walk::kNearbyCount);
    std::uint8_t pushed = 0;
    void* done = manager;
    for (char* node = At<char*>(list.head, 0); node != list.head; node = At<char*>(node, 0)) {
        if (pushed)
            break;
        char* const other = At<char*>(node, 8);
        if (other != mine) {
            std::uint32_t words[3];
            for (std::uint32_t& w : words)
                w = Global<std::uint32_t>(addr::cSC4Pedestrian_DefaultFloat);
            Virtual<std::uint32_t>(other, wslot::kOccupantPoint, static_cast<void*>(words));
            float to[3];
            std::memcpy(to, words, sizeof to);
            const double dx = static_cast<double>(to[0]) - point[0];
            const double dy = static_cast<double>(to[1]) - point[1];
            const double dz = static_cast<double>(to[2]) - point[2];
            to[0] = static_cast<float>(dx);
            to[1] = static_cast<float>(dy);
            to[2] = static_cast<float>(dz);
            const double length = std::sqrt((dy * dy + dz * dz) + dx * dx);
            if (length < one) {
                float y, z;
                if (length < static_cast<double>(Global<float>(addr::cSC4Pedestrian_BumpNear))) {
                    const double half = Global<double>(addr::SC4_DoubleHalf);
                    const double minusHalf = Global<double>(addr::cSC4Pedestrian_MinusHalfDouble);
                    to[0] = static_cast<float>(Draw(minusHalf, half));
                    z = static_cast<float>(Draw(minusHalf, half));
                    y = Global<float>(addr::SC4_FloatZero);
                } else {
                    y = to[1];
                    z = to[2];
                }
                At<std::uint8_t>(other, walk::kOtherByte) = 1;
                At<float>(other, walk::kOtherPoint + 4) = y;
                std::memcpy(other + walk::kOtherPoint, &to[0], 4);
                At<float>(other, walk::kOtherPoint + 8) = z;
                pushed = 1;
            }
        }
        done = list.word;
    }
    Virtual<std::uint32_t>(done, wslot::kListDone, static_cast<void*>(&list));
    CallThis<void>(addr::SC4List_Clear2, static_cast<void*>(&list));
    Virtual<std::uint32_t>(NodeAllocator(), 0x10, static_cast<void*>(list.head));
    return pushed;
}

// 0x00545D60, the step (direction, seconds), ret 8: the traffic network map
// (the city's slot 40) first; the step slot 34 x slot 125 (as a float) x
// seconds stored as a float; the direction scaled to it when its square is
// above 0 (z stored as a float, x and y unrounded) added to the position as
// floats. Without the automata flag 0x20000 the map's slot 44 (position,
// target, 0xC49); the bump (0x00545840: target, seconds); bumped with the
// byte +0x258 and the push +0x25C of a length above 0, the target plus the
// push, slot 44 again and the bump (target, 0). Not bumped: slot 16 (the
// target, the direction as given). The byte +0x258 cleared; EAX 1 when not
// bumped.
std::uint32_t __fastcall PedestrianWalk(char* self, void*, const float* direction, float seconds)
{
    static bool logged;
    FirstCall("PedestrianWalk", logged);
    void* const network = Network();
    float d[3] = {direction[0], direction[1], direction[2]};
    const float speed = static_cast<float>(Virtual<double>(self, wslot::kSlot125));
    const float step =
        static_cast<float>(Virtual<double>(self, wslot::kSmoothedNow) * speed * static_cast<double>(seconds));
    double x = d[0], y = d[1];
    const double square = (static_cast<double>(d[2]) * d[2] + y * y) + x * x;
    if (square > Zero()) {
        const double factor = static_cast<double>(step) / std::sqrt(square);
        x = x * factor;
        y = y * factor;
        d[2] = static_cast<float>(static_cast<double>(d[2]) * factor);
    }
    float* const position = &At<float>(self, base::kPosition);
    float target[3];
    target[0] = static_cast<float>(x + position[0]);
    target[1] = static_cast<float>(y + position[1]);
    target[2] = static_cast<float>(static_cast<double>(d[2]) + position[2]);
    if (!CheckFlag(self, 0x20000u))
        Virtual<std::uint32_t>(network, wslot::kCanGo, static_cast<float*>(position), static_cast<float*>(target),
                               walk::kCanGoKind);
    std::uint8_t bumped = CallThis<std::uint8_t>(addr::cSC4Pedestrian_Bump, self, static_cast<float*>(target), seconds);
    if (bumped && At<std::uint8_t>(self, ped::kByte)) {
        const float* const push = &At<float>(self, ped::kPoint);
        const double px = push[0], py = push[1], pz = push[2];
        if (std::sqrt((px * px + py * py) + pz * pz) > Zero()) {
            target[0] = static_cast<float>(static_cast<double>(target[0]) + push[0]);
            target[1] = static_cast<float>(static_cast<double>(target[1]) + push[1]);
            target[2] = static_cast<float>(static_cast<double>(target[2]) + push[2]);
            Virtual<std::uint32_t>(network, wslot::kCanGo, static_cast<float*>(position), static_cast<float*>(target),
                                   walk::kCanGoKind);
            bumped = CallThis<std::uint8_t>(addr::cSC4Pedestrian_Bump, self, static_cast<float*>(target), 0.0f);
        }
    }
    if (!bumped)
        Virtual<std::uint32_t>(self, wslot::kPlace, static_cast<float*>(target), direction);
    At<std::uint8_t>(self, ped::kByte) = 0;
    return bumped ? 0u : 1u;
}

// 0x00546150, the state 2's frame (seconds), ret 4: the vector +0xA4, when
// its length is above 0, normalised (1 / the length, 0x00B0A00C over it: x
// times it unrounded, y and z times it as a float) and walked (0x00545D60);
// slot 105. AL 1.
std::uint32_t __fastcall PedestrianState2Frame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianState2Frame", logged);
    const float* const v = &At<float>(self, base::kVectorA4);
    const double x = v[0], y = v[1], z = v[2];
    if (std::sqrt((x * x + y * y) + z * z) > Zero()) {
        const double factor = One() / std::sqrt((z * z + y * y) + x * x);
        const float rounded = static_cast<float>(factor);
        float direction[3];
        direction[0] = static_cast<float>(factor * x);
        direction[1] = static_cast<float>(y * rounded);
        direction[2] = static_cast<float>(z * rounded);
        CallThis<std::uint8_t>(addr::cSC4Pedestrian_Walk, self, static_cast<float*>(direction), seconds);
    }
    Virtual<std::uint32_t>(self, pslot::kSlot105);
    return 1;
}

// 0x00545B10, the state 3's frame (seconds), ret 4: AL 0 without a current
// state (slot 56) holding an object (+8). The heading towards the object's
// point (its slot 11 into three 0x00B228DC) in x and z (the length counts the
// unrounded y difference and squares the z difference times its float copy;
// x unrounded and z's float copy times 1 / length, stored as floats). The
// target is the position, plus the push +0x25C with the byte +0x258 (the
// push first scaled to the seconds x 1/360 over its length when its square is
// above 0, stored back; the byte cleared). The traffic network map's slot 44
// (position, target, 0xC49) and the bump (0x00545840); not bumped, slot 16
// (the target, the heading with y 0). With models, the state's names (+0x24)
// and the first model (not null-tested past the list) at its last frame
// (slot 11 against slot 12 less 1, signed), and more than one name
// (unsigned), one drawn (SC4Random::Below) for slot 21. Slot 105, AL 1.
std::uint32_t __fastcall PedestrianState3Frame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianState3Frame", logged);
    char* const current = Virtual<char*>(self, pslot::kCurrentState);
    if (!current || !At<void*>(current, state::kObject))
        return 0;
    float* const position = &At<float>(self, base::kPosition);
    float target[3] = {position[0], position[1], position[2]};
    std::uint32_t words[3];
    for (std::uint32_t& w : words)
        w = Global<std::uint32_t>(addr::cSC4Pedestrian_DefaultFloat);
    Virtual<std::uint32_t>(At<void*>(current, state::kObject), pslot::kStateObjectPoint, static_cast<void*>(words));
    float to[3];
    std::memcpy(to, words, sizeof to);
    const double dx = static_cast<double>(to[0]) - position[0];
    const double dy = static_cast<double>(to[1]) - position[1];
    const double dz = static_cast<double>(to[2]) - position[2];
    const float dzf = static_cast<float>(dz);
    const double factor = One() / std::sqrt((dz * dzf + dy * dy) + dx * dx);
    float heading[3];
    heading[0] = static_cast<float>(dx * factor);
    heading[1] = 0.0f;
    heading[2] = static_cast<float>(static_cast<double>(dzf) * factor);
    if (At<std::uint8_t>(self, ped::kByte)) {
        float* const push = &At<float>(self, ped::kPoint);
        const double px = push[0], py = push[1], pz = push[2];
        const double square = (px * px + py * py) + pz * pz;
        if (square > Zero()) {
            const double scale = static_cast<double>(seconds) * Global<float>(addr::cSC4Pedestrian_PushRate) /
                                 std::sqrt(square);
            push[0] = static_cast<float>(scale * push[0]);
            push[1] = static_cast<float>(scale * push[1]);
            push[2] = static_cast<float>(scale * push[2]);
        }
        target[0] = static_cast<float>(static_cast<double>(target[0]) + push[0]);
        target[1] = static_cast<float>(static_cast<double>(target[1]) + push[1]);
        target[2] = static_cast<float>(static_cast<double>(target[2]) + push[2]);
        At<std::uint8_t>(self, ped::kByte) = 0;
    }
    Virtual<std::uint32_t>(Network(), wslot::kCanGo, static_cast<float*>(position), static_cast<float*>(target),
                           walk::kCanGoKind);
    if (!CallThis<std::uint8_t>(addr::cSC4Pedestrian_Bump, self, static_cast<float*>(target), seconds))
        Virtual<std::uint32_t>(self, wslot::kPlace, static_cast<float*>(target), static_cast<float*>(heading));
    char* const names = At<char*>(current, walk::kNames);
    if (At<char**>(self, base::kModels) != At<char**>(self, base::kModelsEnd) && names) {
        if (char* const first = *At<char**>(self, base::kModels)) {
            const std::int32_t last = Virtual<std::int32_t>(first, pslot::kModelFrames) - 1;
            if (Virtual<std::int32_t>(first, pslot::kModelFrame) >= last) {
                const std::int32_t count =
                    static_cast<std::int32_t>(At<char*>(names, 0x14) - At<char*>(names, 0x10)) / 0x14;
                if (static_cast<std::uint32_t>(count) > 1u) {
                    const std::uint32_t drawn =
                        CallThis<std::uint32_t>(addr::SC4Random_Below, Random(), static_cast<std::uint32_t>(count));
                    Virtual<std::uint32_t>(self, pslot::kSlot21, At<char*>(names, 0x10) + drawn * 0x14u);
                }
            }
        }
    }
    Virtual<std::uint32_t>(self, pslot::kSlot105);
    return 1;
}

// 0x00544950, the state 6's frame (seconds), ret 4: not placed (+0x1F8 null
// and slot 134 AL 0) AL 0. Slot 129 (seconds); before the end of the way
// (slot 87 AL 0) slot 105, AL 1. Else AL 0 unless the current state's object
// (+8; the state not null-tested) answers a word (its slot 6): its point
// (slot 11, into the game's stack unset; ours zeros), the cell of it (the
// city's slot 95), that cell's lot's rectangle (the lot manager's slot 6,
// the lot's slot 14) or else the word's (its slot 11), grown by 1 each way.
// Then AL 0 without the pedestrian manager (slot 174), the occupant +0x1EC or
// the map's automata on it (its slot 16). The groups (slot 136) split
// (0x0053D100); the map's neighbour (slot 15: the occupant, the low byte with
// three bytes of the stack above it (ours zero), 0, 0), AL 0 when none. The
// neighbour's object's point (slot 48's slot 8) and its cell: outside the
// rectangle (SC4Rect::Contains) the path to the occupant (slot 131: the
// occupant, the groups packed with the two low bytes swapped (0x004ADE80),
// 1) and slot 158; inside it, from the side (the low byte) round the sides
// in turn (+1 mod 4) back to it: each but the opposite one (+2) the path to
// the neighbour (slot 131: the neighbour, the key, 1), the first key the
// groups and the next ones packed with the opposite side in the second byte
// and the side in the low one; until one answers AL 1 (slot 105, AL 1). AL
// 0 when none did.
std::uint32_t __fastcall PedestrianState6Frame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianState6Frame", logged);
    if (!At<void*>(self, walk::kPlaced) && !Virtual<std::uint8_t>(self, pslot::kPlaceOnNetwork))
        return 0;
    Virtual<std::uint32_t>(self, wslot::kMoveOnPath, seconds);
    if (!Virtual<std::uint8_t>(self, wslot::kSlot87)) {
        Virtual<std::uint32_t>(self, pslot::kSlot105);
        return 1;
    }
    if (!At<void*>(Virtual<char*>(self, pslot::kCurrentState), state::kObject))
        return 0;
    if (!Virtual<void*>(At<void*>(Virtual<char*>(self, pslot::kCurrentState), state::kObject),
                        pslot::kStateObjectWord))
        return 0;
    float point[3] = {};
    Virtual<std::uint32_t>(At<void*>(Virtual<char*>(self, pslot::kCurrentState), state::kObject),
                           pslot::kStateObjectPoint, static_cast<float*>(point));
    void* const city = Global<void*>(addr::SC4_City);
    std::int32_t cellX = 0, cellZ = 0;
    Virtual<std::uint32_t>(city, wslot::kCellOf, point[0], point[2], &cellX, &cellZ);
    std::int32_t rect[4] = {};
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), wslot::kGetLot, cellX, cellZ, 0u)) {
        Virtual<std::uint32_t>(lot, wslot::kRect, static_cast<std::int32_t*>(rect));
    } else {
        void* const object = At<void*>(Virtual<char*>(self, pslot::kCurrentState), state::kObject);
        Virtual<std::uint32_t>(Virtual<void*>(object, pslot::kStateObjectWord), wslot::kObjectRect,
                               static_cast<std::int32_t*>(rect));
    }
    --rect[0];
    --rect[1];
    ++rect[2];
    ++rect[3];
    void* const network = Network();
    if (!Virtual<void*>(self, wslot::kPedestrianManager))
        return 0;
    void* const occupant = At<void*>(self, base::kOccupant);
    if (!occupant)
        return 0;
    if (!Virtual<void*>(network, wslot::kAutomataOn, occupant))
        return 0;
    const std::uint32_t groups = Virtual<std::uint32_t>(self, pslot::kGroups);
    std::int32_t top = 0;
    std::uint8_t byte1 = 0, side = 0, byte2 = 0;
    CallCdecl<void>(addr::SC4_UnpackGroups, groups, &top, &byte1, &side, &byte2);
    void** const next = Virtual<void**>(network, wslot::kNeighbour, occupant, static_cast<std::uint32_t>(side), 0u, 0u);
    if (!next || !*next)
        return 0;
    void* const neighbour = *next;
    float there[3] = {};
    Virtual<std::uint32_t>(Virtual<void*>(neighbour, wslot::kOccupantObject), wslot::kOccupantPoint,
                           static_cast<float*>(there));
    std::int32_t thereX = 0, thereZ = 0;
    Virtual<std::uint32_t>(city, wslot::kCellOf, there[0], there[2], &thereX, &thereZ);
    if (!CallThis<std::uint8_t>(addr::SC4Rect_Contains, static_cast<void*>(rect), thereX, thereZ)) {
        const std::uint32_t key = CallCdecl<std::uint32_t>(addr::SC4_PackGroups, top, static_cast<std::uint32_t>(side),
                                                           static_cast<std::uint32_t>(byte1),
                                                           static_cast<std::uint32_t>(byte2));
        Virtual<std::uint32_t>(self, pslot::kPathTo, occupant, key, 1u);
        Virtual<std::uint32_t>(self, pslot::kSlot158);
        Virtual<std::uint32_t>(self, pslot::kSlot105);
        return 1;
    }
    const std::uint8_t opposite = static_cast<std::uint8_t>((side + 2) & 3);
    std::uint8_t at = side;
    std::uint32_t key = groups;
    std::uint8_t found = 0;
    for (;;) {
        if (at != opposite)
            found = Virtual<std::uint8_t>(self, pslot::kPathTo, neighbour, key, 1u);
        at = static_cast<std::uint8_t>((at + 1) & 3);
        key = CallCdecl<std::uint32_t>(addr::SC4_PackGroups, top, static_cast<std::uint32_t>(opposite),
                                       static_cast<std::uint32_t>(at), static_cast<std::uint32_t>(byte2));
        if (found) {
            Virtual<std::uint32_t>(self, pslot::kSlot105);
            return 1;
        }
        if (at == side)
            return 0;
    }
}

// The side steps (0x00A9A9EC x, 0x00A9A9DC z) and the order the state 8's
// frame tries the sides in (0x00A9A9FC).
std::int32_t SideX(std::int32_t side) { return Global<std::int32_t>(addr::cSC4Pedestrian_SideX + 4 * side); }
std::int32_t SideZ(std::int32_t side) { return Global<std::int32_t>(addr::cSC4Pedestrian_SideZ + 4 * side); }

// The sway of the states 8 and 9: the heading plus its perpendicular (-z, 0,
// x) scaled to `drawn` over its length when the square (z first) is above 0,
// y then 0 times that factor; the sums stored as floats, z's left unrounded
// in `z`.
void Sway(float* p, float drawn, float& y, double& z)
{
    const float qx = -p[2], qz = p[0];
    double a = qz, b = qx;
    y = 0.0f;
    const double square = static_cast<double>(qz) * qz + static_cast<double>(qx) * qx;
    if (square > Zero()) {
        const double m = static_cast<double>(drawn) / std::sqrt(square);
        b = b * m;
        y = static_cast<float>(Zero() * m);
        a = a * m;
    }
    p[0] = static_cast<float>(b + p[0]);
    z = a + p[2];
}

// 0x00546220, the state 8's frame (seconds, start), ret 8: the current state
// (slot 56, not null-tested); the position's cell (x and z times 0.0625,
// __ftol2); without an occupant on it (the traffic network map's slot 14) slot
// 13 (0), AL 0. Starting (the low byte): the state's side +0x30 drawn
// (SC4Random::Below 4), its cell +0x28, +0x2C -1, and the smoothed speed
// towards 1/720; AL 1. Else on a cell other than the state's: the four sides
// from its side in the order 0, 1, 3, 2 (+ the side, mod 4) asked of
// 0x00B43D54 (slot 29: the cell beside, 0xFF, the side, 2); the first that
// answers AL 1 kept with the cell, none a side drawn. The heading swayed by a
// draw of -1 to 1 (as the state 9's), then stepped by the side's (x, z) at a
// quarter over its length (y 0 times that), z unrounded; normalised (1 / the
// length unrounded, each stored as a float) and walked (0x00545D60). Slot
// 105, AL 1.
std::uint32_t __fastcall PedestrianState8Frame(char* self, void*, float seconds, std::uint32_t start)
{
    static bool logged;
    FirstCall("PedestrianState8Frame", logged);
    char* const current = Virtual<char*>(self, pslot::kCurrentState);
    const std::int32_t cellX = CellOf(At<float>(self, base::kPosition));
    const std::int32_t cellZ = CellOf(At<float>(self, base::kPosition + 8));
    void** const cell = Virtual<void**>(Network(), wslot::kCellList, cellX, cellZ);
    if (!cell || !*cell) {
        Virtual<std::uint32_t>(self, wslot::kRemove, 0u);
        return 0;
    }
    if (start & 0xFF) {
        At<std::uint8_t>(current, walk::kSide) =
            static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::SC4Random_Below, Random(), 4u));
        At<std::int32_t>(current, walk::kCellX) = -1;
        At<std::int32_t>(current, walk::kCellZ) = -1;
        SmoothTowards(self, kSmoothTarget);
        return 1;
    }
    if (cellX != At<std::int32_t>(current, walk::kCellX) || cellZ != At<std::int32_t>(current, walk::kCellZ)) {
        void* const service = Global<void*>(addr::SC4_Service43D54);
        std::int32_t i = 0;
        for (; i < 4; ++i) {
            const std::int32_t side =
                (Global<std::int32_t>(addr::cSC4Pedestrian_SideOrder + 4 * i) + At<std::uint8_t>(current, walk::kSide)) %
                4;
            if (Virtual<std::uint8_t>(service, wslot::kCellOpen, SideX(side) + cellX, SideZ(side) + cellZ, 0xFFu,
                                      side, 2u)) {
                At<std::uint8_t>(current, walk::kSide) = static_cast<std::uint8_t>(side);
                At<std::int32_t>(current, walk::kCellX) = cellX;
                At<std::int32_t>(current, walk::kCellZ) = cellZ;
                break;
            }
        }
        if (i == 4)
            At<std::uint8_t>(current, walk::kSide) =
                static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::SC4Random_Below, Random(), 4u));
    }
    const float* const h = &At<float>(self, walk::kHeading);
    float p[3] = {h[0], h[1], h[2]};
    const float drawn =
        static_cast<float>(Draw(Global<double>(addr::SC4_MinusOneDouble), Global<double>(addr::SC4_DoubleOne)));
    float y = 0.0f;
    double z = 0.0;
    Sway(p, drawn, y, z);
    p[1] = static_cast<float>(static_cast<double>(y) + p[1]);
    p[2] = static_cast<float>(z);
    const std::uint8_t side = At<std::uint8_t>(current, walk::kSide);
    double sx = SideX(side), sz = SideZ(side);
    y = 0.0f;
    const double square = sz * sz + sx * sx;
    if (square > Zero()) {
        const double n = static_cast<double>(Global<float>(addr::cSC4AutomatonBase_Quarter)) / std::sqrt(square);
        sx = sx * n;
        y = static_cast<float>(Zero() * n);
        sz = sz * n;
    }
    p[0] = static_cast<float>(sx + p[0]);
    p[1] = static_cast<float>(static_cast<double>(y) + p[1]);
    const double pz = sz + p[2];
    const double factor = One() / std::sqrt((pz * pz + static_cast<double>(p[1]) * p[1]) + static_cast<double>(p[0]) * p[0]);
    p[0] = static_cast<float>(p[0] * factor);
    p[1] = static_cast<float>(p[1] * factor);
    p[2] = static_cast<float>(pz * factor);
    CallThis<std::uint8_t>(addr::cSC4Pedestrian_Walk, self, static_cast<float*>(p), seconds);
    Virtual<std::uint32_t>(self, pslot::kSlot105);
    return 1;
}

// 0x00546530, the state 9's frame (seconds), ret 4: without +0xB0, down the
// state stack from its top the first state 9 whose object (+8) answers a word
// (its slot 6) gives it (+0xB0 let go when set, then that word's
// QueryInterface 0xA9B40F05 into it); still none, slot 105 and AL 1. The
// position's cell as the state 8's; without an occupant on it slot 13 (0),
// AL 0. The heading with the byte +0x258: x and z two draws of -1 to 1, y 0;
// else swayed by a draw of -0.6 to 0.6. +0xB0's point (its slot 7 into three
// 0x00B228DC) less the position in x and z (y 0), stored as floats; beyond
// 10 (0x00A94D50) its x, y and z times 1 x 1/64 added (x and y stored, z
// unrounded). Normalised as the state 8's, the smoothed speed towards 1/360
// and walked (0x00545D60). Slot 105, AL 1.
std::uint32_t __fastcall PedestrianState9Frame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PedestrianState9Frame", logged);
    if (!At<void*>(self, walk::kObjectB0)) {
        const std::int32_t count =
            static_cast<std::int32_t>(At<char*>(self, base::kStatesEnd) - At<char*>(self, base::kStates)) >> 2;
        for (std::int32_t i = count - 1; i >= 0; --i) {
            char* const s = At<char**>(self, base::kStates)[i];
            if (At<std::uint32_t>(s, state::kType) != 9)
                continue;
            void* const object = At<void*>(s, state::kObject);
            if (!object)
                continue;
            void* const word = Virtual<void*>(object, pslot::kStateObjectWord);
            if (!word)
                continue;
            if (void* const old = At<void*>(self, walk::kObjectB0)) {
                At<void*>(self, walk::kObjectB0) = nullptr;
                Release(old);
            }
            Virtual<std::uint8_t>(word, slot::kQueryInterface, walk::kTargetIid,
                                  static_cast<void*>(&At<void*>(self, walk::kObjectB0)));
            break;
        }
        if (!At<void*>(self, walk::kObjectB0)) {
            Virtual<std::uint32_t>(self, pslot::kSlot105);
            return 1;
        }
    }
    const std::int32_t cellX = CellOf(At<float>(self, base::kPosition));
    const std::int32_t cellZ = CellOf(At<float>(self, base::kPosition + 8));
    void** const cell = Virtual<void**>(Network(), wslot::kCellList, cellX, cellZ);
    if (!cell || !*cell) {
        Virtual<std::uint32_t>(self, wslot::kRemove, 0u);
        return 0;
    }
    const float* const h = &At<float>(self, walk::kHeading);
    float p[3] = {h[0], h[1], h[2]};
    if (At<std::uint8_t>(self, ped::kByte)) {
        p[1] = 0.0f;
        const double low = Global<double>(addr::SC4_MinusOneDouble), high = Global<double>(addr::SC4_DoubleOne);
        p[0] = static_cast<float>(Draw(low, high));
        p[2] = static_cast<float>(Draw(low, high));
    } else {
        const float drawn = static_cast<float>(
            Draw(Global<double>(addr::cSC4Pedestrian_SwayLow), Global<double>(addr::cSC4Pedestrian_SwayHigh)));
        float y = 0.0f;
        double z = 0.0;
        Sway(p, drawn, y, z);
        p[1] = static_cast<float>(static_cast<double>(y) + p[1]);
        p[2] = static_cast<float>(z);
    }
    std::uint32_t words[3];
    for (std::uint32_t& w : words)
        w = Global<std::uint32_t>(addr::cSC4Pedestrian_DefaultFloat);
    Virtual<std::uint32_t>(At<void*>(self, walk::kObjectB0), wslot::kTargetPoint, static_cast<void*>(words));
    float t[3];
    std::memcpy(t, words, sizeof t);
    const float* const position = &At<float>(self, base::kPosition);
    t[0] = static_cast<float>(static_cast<double>(t[0]) - position[0]);
    t[1] = 0.0f;
    t[2] = static_cast<float>(static_cast<double>(t[2]) - position[2]);
    double z = p[2];
    if (std::sqrt(static_cast<double>(t[2]) * t[2] + static_cast<double>(t[0]) * t[0]) >
        static_cast<double>(Global<float>(addr::cSC4AutomatonBase_QueueScale))) {
        const double pull = One() * Global<float>(addr::cSC4Pedestrian_TargetPull);
        t[0] = static_cast<float>(t[0] * pull);
        t[1] = static_cast<float>(Zero() * pull);
        t[2] = static_cast<float>(t[2] * pull);
        p[0] = static_cast<float>(static_cast<double>(t[0]) + p[0]);
        p[1] = static_cast<float>(static_cast<double>(t[1]) + p[1]);
        z = static_cast<double>(t[2]) + p[2];
    }
    const double factor = One() / std::sqrt((z * z + static_cast<double>(p[1]) * p[1]) + static_cast<double>(p[0]) * p[0]);
    p[0] = static_cast<float>(p[0] * factor);
    p[1] = static_cast<float>(p[1] * factor);
    p[2] = static_cast<float>(z * factor);
    SmoothTowards(self, walk::kRunTarget);
    CallThis<std::uint8_t>(addr::cSC4Pedestrian_Walk, self, static_cast<float*>(p), seconds);
    Virtual<std::uint32_t>(self, pslot::kSlot105);
    return 1;
}

// ====================================================================================
// The one-shot and the riot police (ped3)

std::uint32_t Below(std::uint32_t count) { return CallThis<std::uint32_t>(addr::SC4Random_Below, Random(), count); }

std::uint32_t Mission(char* self) { return Virtual<std::uint32_t>(At<char*>(self, crew::kVehicle), cslot::kMission); }

// The car's occupant part (+4), or null without a car.
void* CarPart(char* self)
{
    char* const car = At<char*>(self, crew::kVehicle);
    return car ? car + 4 : nullptr;
}

// The message helper at its address.
void Post(char* self, std::uint32_t type, void* object, std::uint32_t a, std::uint32_t b, std::uint32_t c,
          std::uint32_t d)
{
    CallThis<void>(addr::cSC4RiotPolice_PostMessage, self, type, object, a, b, c, d);
}

// The mission 1's model key: 0x00A9AE38 by +0x27A x 3 plus a draw below 3
// (the byte's product and sum, as the game's 8-bit IMUL and ADD).
void DrawnKey(char* self, std::uint32_t (&key)[3])
{
    const std::uint32_t drawn = Below(3);
    const std::uint8_t index =
        static_cast<std::uint8_t>(At<std::uint8_t>(self, riot::kByte27A) * 3 + static_cast<std::uint8_t>(drawn));
    key[0] = crewdata::kDrawnKeyType;
    key[1] = crewdata::kDrawnKeyGroup;
    key[2] = reinterpret_cast<const std::uint32_t*>(addr::cSC4RiotPolice_DrawnModels)[index];
}

// The camera view's direction (+0x54 to +0x5C) negated.
void AwayFromCamera(float (&heading)[3])
{
    char* const camera = Global<char*>(addr::SC4_CameraView);
    for (int i = 0; i < 3; ++i)
        heading[i] = -At<float>(camera, cslot::kCameraDirection + 4 * i);
}

// 0x00544DA0, the one-shot's slot 3: slot 158 unless the occupant part (+4)
// says the object is initialised (its slot 5); the automaton base's Init
// 0x0054D790, its EAX when it answers AL 0. Else each model (not null) given
// 0.01 (slot 14) and the byte +0x270 drawn from low to high inclusive
// (SC4Random::Below, the count high - low + 1 as an int): the bytes
// 0x00B21D04 and 0x00B21D05, or the first two of the occupant's property
// 0xEA958845 when its value holds more than one (the part's slot 6, the
// holder's slot 9, the property's slot 5, the value's slots 95 and 4; the
// property null-tested, the value not). AL the Init's (AL only in the game;
// ours zero-extends).
std::uint32_t __fastcall OneShotInit(char* self, void*)
{
    static bool logged;
    FirstCall("OneShotInit", logged);
    if (!Virtual<std::uint8_t>(self + 4, pslot::kIsInitialised))
        Virtual<std::uint32_t>(self, pslot::kSlot158);
    const std::uint32_t init = CallThis<std::uint32_t>(addr::cSC4AutomatonBase_Init, self);
    if (!(init & 0xFF))
        return init;
    for (char** it = At<char**>(self, base::kModels); it != At<char**>(self, base::kModelsEnd); ++it)
        if (char* const model = *it)
            Virtual<std::uint32_t>(model, pslot::kModelSlot14, 0x3C23D70Au); // 0.01f
    std::uint8_t high = Global<std::uint8_t>(addr::cSC4PedestrianOneShot_CountHigh);
    std::uint8_t low = Global<std::uint8_t>(addr::cSC4PedestrianOneShot_CountLow);
    if (void* const holder = Virtual<void*>(self + 4, cslot::kPropertyHolder))
        if (void* const property = Virtual<void*>(holder, cslot::kGetProperty, crewdata::kCountProperty)) {
            void* const value = Virtual<void*>(property, cslot::kPropertyValue);
            const std::uint8_t* const bytes = Virtual<const std::uint8_t*>(value, cslot::kValueBytes);
            if (Virtual<std::uint32_t>(value, cslot::kValueCount) > 1) {
                low = bytes[0];
                high = bytes[1];
            }
        }
    const std::uint32_t drawn = Below(static_cast<std::uint32_t>(static_cast<std::int32_t>(high) - low + 1));
    At<std::uint8_t>(self, oneshot::kByte) = static_cast<std::uint8_t>(drawn + low);
    return init & 0xFF;
}

// 0x00544E80, the one-shot's slot 72 (word, seconds), ret 8: slot 83
// (seconds), then each model (not null) advanced (slot 15, seconds); the
// first one at its last frame (slot 12 less 1, then slot 11, signed) counts
// the byte +0x270 down (not below 0) and at 0 removes the one-shot (slot 13
// with 0), AL 0. Else AL 1. The word is not read.
std::uint32_t __fastcall OneShotFrame(char* self, void*, std::uint32_t, float seconds)
{
    static bool logged;
    FirstCall("OneShotFrame", logged);
    std::uint32_t answer = 1;
    Virtual<std::uint32_t>(self, cslot::kSlot83, seconds);
    bool first = true;
    for (char** it = At<char**>(self, base::kModels); it != At<char**>(self, base::kModelsEnd); ++it) {
        char* const model = *it;
        if (!model)
            continue;
        Virtual<std::uint32_t>(model, pslot::kModelAdvance, seconds);
        if (first) {
            const std::int32_t last = Virtual<std::int32_t>(model, pslot::kModelFrames) - 1;
            if (Virtual<std::int32_t>(model, pslot::kModelFrame) >= last) {
                std::uint8_t& count = At<std::uint8_t>(self, oneshot::kByte);
                if (count)
                    --count;
                if (!count) {
                    Virtual<std::uint32_t>(self, wslot::kRemove, 0u);
                    answer = 0;
                }
            }
        }
        first = false;
    }
    return answer;
}

// 0x00547630, the riot police's slot 3: AL 1 when the occupant part (+4)
// says the object is initialised (its slot 5). Else by the car's mission
// (+0x270's slot 26; the car not null-tested):
//   5: the level +0x278 counted up from 0 while the float +0x274 is below
//      (or unordered with) 0x00B21CD0[level], to 3 at most; at level 2 the
//      message 0xA9E3B6E2 (the part, -1, 0x40, 0, 0); the start model
//      (SC4Automaton::SetStart) 0x29A5D1EC, 0x49A593E7, 0x00A9AE28[level].
//   1: +0x27A drawn below 6; the start model 0x5AD0E817, 0xBADB57F1,
//      0x00A9AE38[+0x27A x 3 + a draw below 3].
// Then the city's sounds (its slot 47, the city not null-tested) told the
// sound "riot_police_repulsor" (SC4String::FromText, freed after) for slot
// 5's answer (their slot 5 with 0; the sound it answers not kept); the
// automata flags 0x2000 and 0x4000 on; with a start model (its instance not
// 0), slot 174's object's slot 44 (the key) and, when it finds one, that
// one's +0x30 given to the +0x20 part's slot 3. Then the pedestrian's Init
// 0x005444F0, its EAX.
std::uint32_t __fastcall RiotPoliceInit(char* self, void*)
{
    static bool logged;
    FirstCall("RiotPoliceInit", logged);
    if (Virtual<std::uint8_t>(self + 4, pslot::kIsInitialised))
        return 1;
    std::uint32_t key[3] = {};
    switch (Mission(self)) {
    case 5: {
        std::uint8_t& level = At<std::uint8_t>(self, crew::kByte);
        level = 0;
        const float* const levels = reinterpret_cast<const float*>(addr::cSC4RiotPolice_Levels);
        while (!(At<float>(self, crew::kWord) >= levels[level]))
            if (++level >= 3)
                break;
        if (level == 2)
            Post(self, crewdata::kRepulsorMessage, self + 4, 0xFFFFFFFFu, 0x40u, 0u, 0u);
        key[0] = crewdata::kLevelKeyType;
        key[1] = crewdata::kLevelKeyGroup;
        key[2] = reinterpret_cast<const std::uint32_t*>(addr::cSC4RiotPolice_LevelModels)[level];
        CallThis<std::uint32_t>(addr::SC4Automaton_SetStart, self, static_cast<const void*>(key));
        break;
    }
    case 1:
        At<std::uint8_t>(self, riot::kByte27A) = static_cast<std::uint8_t>(Below(6));
        DrawnKey(self, key);
        CallThis<std::uint32_t>(addr::SC4Automaton_SetStart, self, static_cast<const void*>(key));
        break;
    default:
        break;
    }
    if (void* const sounds = Virtual<void*>(Global<void*>(addr::SC4_City), cslot::kCitySounds)) {
        GameString name{};
        CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&name),
                        reinterpret_cast<const char*>(addr::cSC4RiotPolice_RepulsorName));
        void* const occupant = Virtual<void*>(self, wslot::kAsOccupant);
        Virtual<void*>(sounds, cslot::kSoundsMake, occupant, static_cast<void*>(&name), 0u);
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            CallCdecl<void>(addr::SC4_Free, static_cast<void*>(name.begin));
    }
    SetFlag(self, 0x2000u, 1u);
    SetFlag(self, 0x4000u, 1u);
    if (key[2])
        if (void* const manager = Virtual<void*>(self, wslot::kPedestrianManager))
            if (char* const found = Virtual<char*>(manager, cslot::kManagerFind, static_cast<void*>(key)))
                Virtual<std::uint32_t>(self + 0x20, cslot::kPart20Set, At<std::uint32_t>(found, 0x30));
    return CallThis<std::uint32_t>(addr::cSC4Pedestrian_Init, self);
}

// 0x00546EF0, the riot police's slot 4: when the occupant part (+4) says the
// object is initialised, the message 0xA9E3B6E2 (the part, four zeros);
// then 0x00544520 (a jump to the base's Shutdown, a tail jump), its EAX.
std::uint32_t __fastcall RiotPoliceShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("RiotPoliceShutdown", logged);
    if (Virtual<std::uint8_t>(self + 4, pslot::kIsInitialised))
        Post(self, crewdata::kRepulsorMessage, self + 4, 0u, 0u, 0u, 0u);
    return CallThis<std::uint32_t>(addr::cSC4Pedestrian_Shutdown, self);
}

// 0x00547530, the riot police's slot 72 (word, seconds), ret 8: the
// pedestrian's frame 0x00544580. With the automata flag 1, the message
// 0x8A0F3F7B (the car's +4, 0x00A9AE80[+0x278], three zeros) unless the
// car's mission (slot 26, the car not null-tested) is 5. Without it, on the
// mission 1 and with models, the first model (not null-tested) at its last
// frame (slot 12 less 1, then slot 11, signed) swapped (SwapModel) for the
// mission 1's drawn key. AL the pedestrian frame's (AL only in the game;
// ours zero-extends).
std::uint32_t __fastcall RiotPoliceFrame(char* self, void*, std::uint32_t word, float seconds)
{
    static bool logged;
    FirstCall("RiotPoliceFrame", logged);
    const std::uint8_t framed = CallThis<std::uint8_t>(addr::cSC4Pedestrian_Frame, self, word, seconds);
    if (CheckFlag(self, 1u)) {
        if (Mission(self) != 5)
            Post(self, crewdata::kFrameMessage, CarPart(self),
                 reinterpret_cast<const std::uint32_t*>(addr::cSC4RiotPolice_MessageWords)[At<std::uint8_t>(self, crew::kByte)],
                 0u, 0u, 0u);
        return framed;
    }
    if (Mission(self) != 1 || At<char**>(self, base::kModels) == At<char**>(self, base::kModelsEnd))
        return framed;
    char* const model = *At<char**>(self, base::kModels);
    const std::int32_t last = Virtual<std::int32_t>(model, pslot::kModelFrames) - 1;
    if (Virtual<std::int32_t>(model, pslot::kModelFrame) >= last) {
        std::uint32_t key[3];
        DrawnKey(self, key);
        CallThis<void>(addr::cSC4AutomatonCore_SwapModel, self, static_cast<void*>(model), static_cast<const void*>(key));
    }
    return framed;
}

// 0x00546CC0, the riot police's slot 74 (zoom, rotation), ret 8: the core's
// 0x00509660; on the car's mission (slot 26) 1, slot 16 with no point and
// the camera view's direction negated as the heading.
void __fastcall RiotPoliceSetZoomAndRotation(char* self, void*, std::uint32_t zoom, std::uint32_t rotation)
{
    static bool logged;
    FirstCall("RiotPoliceSetZoomAndRotation", logged);
    CallThis<void>(addr::cSC4AutomatonCore_SetZoomAndRotation, self, zoom, rotation);
    if (Mission(self) != 1)
        return;
    float heading[3];
    AwayFromCamera(heading);
    Virtual<std::uint32_t>(self, wslot::kPlace, static_cast<const float*>(nullptr), static_cast<const float*>(heading));
}

// 0x00546D20 (this not read; type, object, four words), ret 0x18: a
// cGZMessage (0x2C bytes of RZ::operator_new) held, given the type (slot 5),
// the object (slot 33) and the words (slots 11, 16, 21 and 26), posted (the
// message server's slot 4, with 0) and released. A failed allocation is
// null-tested only around the AddRef (the game then calls through null).
void __fastcall RiotPolicePostMessage(char*, void*, std::uint32_t type, void* object, std::uint32_t a,
                                      std::uint32_t b, std::uint32_t c, std::uint32_t d)
{
    static bool logged;
    FirstCall("RiotPolicePostMessage", logged);
    char* const block = CallCdecl<char*>(addr::RZ_operator_new, std::size_t{crewdata::kMessageSize});
    char* const message = block ? CallThis<char*>(addr::cGZMessage_cGZMessage, block) : nullptr;
    if (message)
        Virtual<std::uint32_t>(message, slot::kAddRef);
    Virtual<std::uint32_t>(message, cslot::kMessageType, type);
    Virtual<std::uint32_t>(message, cslot::kMessageObject, object);
    Virtual<std::uint32_t>(message, cslot::kMessageData1, a);
    Virtual<std::uint32_t>(message, cslot::kMessageData2, b);
    Virtual<std::uint32_t>(message, cslot::kMessageData3, c);
    Virtual<std::uint32_t>(message, cslot::kMessageData4, d);
    Virtual<std::uint32_t>(Global<void*>(addr::SC4_MessageServer), cslot::kPost, static_cast<void*>(message), 0u);
    Release(message);
}

// 0x00547930 (this not read; point), ret 4: with a city, the point's x and
// z kept in it: below 0 (0x00A81054, NaN not) set to 0; else at or past the
// city's cells (slots 93 and 94, shifted left 4 as ints, then floats) set
// to that less 1 (0x00A81228) as a float.
void __fastcall RiotPoliceClampToCity(char*, void*, float* point)
{
    static bool logged;
    FirstCall("RiotPoliceClampToCity", logged);
    void* const city = Global<void*>(addr::SC4_City);
    if (!city)
        return;
    const float sizes[2] = {
        static_cast<float>(static_cast<std::int32_t>(Virtual<std::uint32_t>(city, cslot::kCityCellsX) << 4)),
        static_cast<float>(static_cast<std::int32_t>(Virtual<std::uint32_t>(city, cslot::kCityCellsZ) << 4))};
    const double zero = Zero();
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    for (int i = 0; i < 2; ++i) {
        float& v = point[2 * i];
        if (static_cast<double>(v) < zero)
            v = 0.0f;
        else if (v >= sizes[i])
            v = static_cast<float>(static_cast<double>(sizes[i]) - one);
    }
}

// An x87 value as the FPU keeps it, its ten bytes: FSIN and FCOS leave 64
// bits of mantissa whatever the precision control, and the dispatch
// multiplies them as they are (as the tank's and the train's frames).
struct X87 {
    unsigned char bytes[10];
};

// The sine and the cosine of `angle` (FSIN, FCOS).
void X87SinCos(double angle, X87* sine, X87* cosine)
{
    __asm {
        mov eax, sine
        mov edx, cosine
        fld qword ptr [angle]
        fld st(0)
        fcos
        fstp tbyte ptr [edx]
        fsin
        fstp tbyte ptr [eax]
    }
}

// `value` times `factor` (FMUL).
double X87Times(const X87& value, double factor)
{
    double product = 0.0;
    double* const out = &product;
    const X87* const in = &value;
    __asm {
        mov eax, in
        mov edx, out
        fld tbyte ptr [eax]
        fmul qword ptr [factor]
        fstp qword ptr [edx]
    }
    return product;
}

// A model transform as the car's slot 12 fills it: a count and flags, a 3x3
// matrix, a point and a scale.
struct ModelTransform {
    std::uint8_t count, flags;
    std::uint8_t unused[2];
    float m[9];
    float point[3];
    float scale;
};
static_assert(sizeof(ModelTransform) == 0x38, "slot 12's transform");

// The float with its sign flipped (FCHS) as a stored value, so that the
// compiler cannot move the flip past the product that follows it (which
// would leave a NaN's sign as it was).
float Negated(float value)
{
    volatile float negated = -value;
    return negated;
}

// Three copies of the float at `address` (its bits): 0x00B228E4 the riot
// police's, 0x00B228F0 the fire crew's.
void DefaultPoint(float (&p)[3], std::uintptr_t address = addr::cSC4RiotPolice_DefaultFloat)
{
    const std::uint32_t bits = Global<std::uint32_t>(address);
    for (float& v : p)
        std::memcpy(&v, &bits, sizeof v);
}

// The float of an x87 value, rounded once (FSTP to a float).
float X87ToFloat(const X87& value)
{
    float out = 0.0f;
    float* const to = &out;
    const X87* const in = &value;
    __asm {
        mov eax, in
        mov edx, to
        fld tbyte ptr [eax]
        fstp dword ptr [edx]
    }
    return out;
}

// The map's height (slot 18) at the point's x and z, into its y.
void OnMap(void* map, float* point)
{
    point[1] = static_cast<float>(Virtual<double>(map, cslot::kMapHeight, point[0], point[2]));
}

// The dispatch on the mission 5. The direction from the car's position
// (slot 7) to its point (slot 143): x rounded to a float, y and z not; the
// length squares the float x, then z, then y; shorter than 1e-5 (not
// unordered) it is scaled by 0x00B0A028 / length (each a float), else the
// car's heading (slot 15) replaces it. The target is the position plus 8
// (0x00A8E178) times it (x unrounded, y and z as floats), kept in the city,
// on the map's height, placed heading that way, and the message
// 0xAA0F3F75.
void DispatchRiot(char* self, void* map)
{
    float p[3], q[3], r[3];
    DefaultPoint(p);
    DefaultPoint(q);
    DefaultPoint(r);
    Virtual<std::uint32_t>(At<char*>(self, crew::kVehicle), cslot::kCarPosition, static_cast<float*>(p));
    const float* const t = Virtual<const float*>(At<char*>(self, crew::kVehicle), cslot::kCarPoint);
    const float t0 = t[0], t1 = t[1], t2 = t[2];
    const float dx = static_cast<float>(static_cast<double>(t0) - p[0]);
    q[0] = dx;
    const double dy = static_cast<double>(t1) - p[1];
    const double dz = static_cast<double>(t2) - p[2];
    q[1] = static_cast<float>(dy);
    q[2] = static_cast<float>(dz);
    const double length =
        std::sqrt((static_cast<double>(dx) * dx + dz * dz) + dy * dy);
    if (length < static_cast<double>(Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon))) {
        const double factor = static_cast<double>(Global<float>(addr::cSC4RiotPolice_One)) / length;
        q[0] = static_cast<float>(static_cast<double>(dx) * factor);
        q[1] = static_cast<float>(dy * factor);
        q[2] = static_cast<float>(dz * factor);
    } else {
        Virtual<std::uint32_t>(At<char*>(self, crew::kVehicle), cslot::kCarHeading, static_cast<float*>(q));
    }
    const double eight = Global<float>(addr::SC4_FloatEight);
    const double x = static_cast<double>(q[0]) * eight + p[0];
    const float y8 = static_cast<float>(static_cast<double>(q[1]) * eight);
    const float z8 = static_cast<float>(static_cast<double>(q[2]) * eight);
    r[2] = static_cast<float>(static_cast<double>(z8) + p[2]);
    r[0] = static_cast<float>(x);
    r[1] = static_cast<float>(static_cast<double>(y8) + p[1]);
    CallThis<void>(addr::cSC4RiotPolice_ClampToCity, self, static_cast<float*>(r));
    OnMap(map, r);
    Virtual<std::uint32_t>(self, wslot::kPlace, static_cast<const float*>(r), static_cast<const float*>(q));
    Post(self, crewdata::kDispatchMessage, CarPart(self),
         reinterpret_cast<const std::uint32_t*>(addr::cSC4RiotPolice_MessageWords)[At<std::uint8_t>(self, crew::kByte)],
         0u, 0u, 0u);
}

// How the riot police's and the fire crew's dispatches place a crew beside
// the car: the float its points start from, the distance, whether a car
// without an occupant removes the crew, and the cell x when the city's slot
// 95 leaves it (the game's local; the fire crew's holds the map).
struct Beside {
    std::uintptr_t start;
    std::uintptr_t distance;
    bool removeAlone;
    std::int32_t cellX;
};

// The dispatch on the mission 1 (the riot police's) or any but 5 (the fire
// crew's). The car's position (slot 7) and transform (slot 12, from the
// identity, a count and flags of 0) turned by the double 0x00A99090 (the
// matrix's columns 0 and 2 by its FCOS and FSIN at 64 bits, each product at
// 53 bits, stored as floats where the game stores them); the side is
// ((m1 + c0) x 0 - c2) x scale for the row (m1, m4, m7) and the new column
// (c0, c1, c2) (x keeps c2 unrounded, y and z its float; 0x00A81054 and
// 0x00A81228 the zero and the one); the target the position plus the
// distance (the riot police's 6 0x00A90900, the fire crew's 0x00B21CCC)
// times it (x unrounded, y and z as floats). Without the car's occupant
// (slot 137) the riot police are removed (Remove(1)) and the fire crew goes
// on. Else when the target's cell (city slot 95, the cells the game's unset
// locals: ours zero, the fire crew's x the map's address as its stack holds
// it) is outside the rectangle of the occupant's object (its slot 48, then
// slot 11, signed), the target is the distance the other way (x and y as
// floats, z unrounded). Kept in the city, on the map's height, placed (slot
// 16) heading against the camera view.
void DispatchBeside(char* self, void* map, const Beside& how)
{
    float p[3], q[3];
    DefaultPoint(p, how.start);
    DefaultPoint(q, how.start);
    ModelTransform transform{};
    static constexpr float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(transform.m, kIdentity, sizeof kIdentity);
    transform.scale = 1.0f;
    Virtual<std::uint32_t>(At<char*>(self, crew::kVehicle), cslot::kCarPosition, static_cast<float*>(p));
    Virtual<std::uint32_t>(At<char*>(self, crew::kVehicle), cslot::kCarTransform, static_cast<void*>(&transform));
    X87 sine, cosine;
    X87SinCos(Global<double>(addr::cSC4RiotPolice_QuarterTurn), &sine, &cosine);
    float* const m = transform.m;
    const float m0 = m[0], m2 = m[2], m3 = m[3], m5 = m[5], m6 = m[6], m8 = m[8];
    const float m8s = static_cast<float>(X87Times(sine, m8));
    const double m0c = X87Times(cosine, m0);
    const double m2s = X87Times(sine, m2);
    const double m5s = X87Times(sine, m5);
    const float m3c = static_cast<float>(X87Times(cosine, m3));
    const float m6c = static_cast<float>(X87Times(cosine, m6));
    float r[3];
    r[0] = static_cast<float>(m0c - m2s);
    m[0] = r[0];
    const double c1 = static_cast<double>(m3c) - m5s;
    r[1] = static_cast<float>(c1);
    const double c2 = static_cast<double>(m6c) - m8s;
    r[2] = static_cast<float>(c2);
    m[3] = static_cast<float>(c1);
    m[6] = static_cast<float>(c2);
    const float m2c = static_cast<float>(X87Times(cosine, m2));
    const float m5c = static_cast<float>(X87Times(cosine, m5));
    const float m8c = static_cast<float>(X87Times(cosine, m8));
    const float m0s = static_cast<float>(X87Times(sine, m0));
    const float m3s = static_cast<float>(X87Times(sine, m3));
    const double m6s = X87Times(sine, m6);
    const double n0 = static_cast<double>(m0s) + m2c;
    const double n1 = static_cast<double>(m3s) + m5c;
    const double n2 = m6s + m8c;
    m[2] = static_cast<float>(n0);
    m[5] = static_cast<float>(n1);
    m[8] = static_cast<float>(n2);
    const double zero = Zero();
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    const double v0 = (static_cast<double>(m[1]) + r[0]) * zero - n0 * one;
    const double v1 = (static_cast<double>(m[4]) + r[1]) * zero - static_cast<double>(m[5]) * one;
    const float v2 = static_cast<float>((static_cast<double>(m[7]) + r[2]) * zero - static_cast<double>(m[8]) * one);
    const double scale = transform.scale;
    r[0] = static_cast<float>(v0 * scale);
    r[1] = static_cast<float>(v1 * scale);
    r[2] = static_cast<float>(static_cast<double>(v2) * scale);
    const double six = Global<float>(how.distance);
    const double x6 = static_cast<double>(r[0]) * six;
    const float y6 = static_cast<float>(static_cast<double>(r[1]) * six);
    const float z6 = static_cast<float>(static_cast<double>(r[2]) * six);
    q[2] = static_cast<float>(static_cast<double>(z6) + p[2]);
    q[0] = static_cast<float>(x6 + p[0]);
    q[1] = static_cast<float>(static_cast<double>(y6) + p[1]);
    char* const occupant = Virtual<char*>(At<char*>(self, crew::kVehicle), cslot::kCarOccupant);
    if (!occupant && how.removeAlone) {
        Virtual<std::uint32_t>(self, wslot::kRemove, 1u);
        return;
    }
    if (occupant) {
        void* const object = Virtual<void*>(occupant, wslot::kOccupantObject);
        std::int32_t rect[4] = {};
        Virtual<std::uint32_t>(object, wslot::kObjectRect, static_cast<std::int32_t*>(rect));
        std::int32_t cellX = how.cellX, cellZ = 0;
        Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), wslot::kCellOf, q[0], q[2], &cellX, &cellZ);
        if (!(cellX >= rect[0] && cellX <= rect[2] && cellZ >= rect[1] && cellZ <= rect[3])) {
            const float nx = static_cast<float>(static_cast<double>(Negated(r[0])) * six);
            const float ny = static_cast<float>(static_cast<double>(Negated(r[1])) * six);
            const double nz = static_cast<double>(Negated(r[2])) * six;
            q[2] = static_cast<float>(nz + p[2]);
            q[0] = static_cast<float>(static_cast<double>(nx) + p[0]);
            q[1] = static_cast<float>(static_cast<double>(ny) + p[1]);
        }
    }
    CallThis<void>(addr::cSC4RiotPolice_ClampToCity, self, static_cast<float*>(q));
    OnMap(map, q);
    float heading[3];
    AwayFromCamera(heading);
    Virtual<std::uint32_t>(self, wslot::kPlace, static_cast<const float*>(q), static_cast<const float*>(heading));
}

// 0x00546F30: the city's map (slot 40) held for the while (its slot 18 is
// called without a null test); by the car's mission (slot 26, the car not
// null-tested) 5 or 1 the riot police placed (above); then slot 33 (0, 0)
// and (1, 1).
void __fastcall RiotPoliceDispatch(char* self, void*)
{
    static bool logged;
    FirstCall("RiotPoliceDispatch", logged);
    void* const map = Virtual<void*>(Global<void*>(addr::SC4_City), wslot::kCityNetwork);
    if (map)
        Virtual<std::uint32_t>(map, slot::kAddRef);
    switch (Mission(self)) {
    case 5:
        DispatchRiot(self, map);
        break;
    case 1:
        DispatchBeside(self, map, Beside{addr::cSC4RiotPolice_DefaultFloat, addr::SC4_FloatSix, true, 0});
        break;
    default:
        break;
    }
    Virtual<std::uint32_t>(self, cslot::kFade, 0.0f, 0.0f);
    Virtual<std::uint32_t>(self, cslot::kFade, 1.0f, 1.0f);
    if (map)
        Release(map);
}

// ====================================================================================
// The fire crew's dispatch (ped4a)

// A model's virtual base (its +4 table's second word past +4): the drawable
// whose slots 1, 2, 9 and 10 the dispatch calls.
char* ModelBase(char* model) { return model + 4 + At<std::int32_t>(At<char*>(model, 4), 4); }

// The truck's heading turned a quarter about y (the hose's state): the
// double 0x00A99090 (pi/2) or, on a draw of the core's generator at or above
// 0x7FFFFFFF, 0x00A8EDD8 (-pi/2), FSIN and FCOS at 64 bits. The game's
// rotation about the axis (0, 1, 0) as the zero 0x00A81054 leaves it: each
// term stored as a float where the game stores it, the sums at 53 bits.
void TurnHeading(float (&q)[3])
{
    const std::uint32_t draw = CallThis<std::uint32_t>(addr::cRZRandomSuper_Next,
                                                       reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random));
    X87 sine, cosine;
    X87SinCos(Global<double>(draw < 0x7FFFFFFFu ? addr::cSC4RiotPolice_QuarterTurn : addr::cSC4Vehicle_PlaceTurn),
              &sine, &cosine);
    const double zero = Zero();
    const float zs = static_cast<float>(X87Times(sine, zero));
    const float s = X87ToFloat(sine);
    const float c = X87ToFloat(cosine);
    const double cz = X87Times(cosine, zero);
    const float czf = static_cast<float>(cz);
    const double a0 = static_cast<double>(c) - zs;
    const float a1 = static_cast<float>(cz - zs);
    const float a2 = static_cast<float>(static_cast<double>(czf) - s);
    const double b0 = static_cast<double>(czf) + s;
    const float b1 = static_cast<float>(static_cast<double>(zs) + czf);
    const float b2 = static_cast<float>(static_cast<double>(zs) + c);
    const float y0 = static_cast<float>(static_cast<double>(q[1]) * zero);
    const float x = static_cast<float>((q[0] * a0 + b0 * q[2]) + y0);
    const float y = static_cast<float>((q[0] * static_cast<double>(a1) + static_cast<double>(b1) * q[2]) + q[1]);
    const float z = static_cast<float>((static_cast<double>(a2) * q[0] + static_cast<double>(b2) * q[2]) + y0);
    q[0] = x;
    q[1] = y;
    q[2] = z;
}

// The heading turned by the double 0x00A99F68 (pi/4) about y, as above: the
// cosine and the sine's float rounded once from 64 bits, z's sine product
// kept unrounded.
void TurnEighth(float (&q)[3])
{
    X87 sine, cosine;
    X87SinCos(Global<double>(addr::cSC4SimFireCrew_EighthTurn), &sine, &cosine);
    const double zero = Zero();
    const float c = X87ToFloat(cosine);
    const double zs = X87Times(sine, zero);
    const float zsf = static_cast<float>(zs);
    const float s = X87ToFloat(sine);
    const double cz = static_cast<double>(c) * zero;
    const float czf = static_cast<float>(cz);
    const float a0 = static_cast<float>(static_cast<double>(c) - zsf);
    const float a1 = static_cast<float>(cz - zsf);
    const float a2 = static_cast<float>(cz - s);
    const float b0 = static_cast<float>(static_cast<double>(s) + czf);
    const float b1 = static_cast<float>(static_cast<double>(zsf) + czf);
    const double b2 = zs + c;
    const float y0 = static_cast<float>(static_cast<double>(q[1]) * zero);
    const float x = static_cast<float>((static_cast<double>(b0) * q[2] + static_cast<double>(a0) * q[0]) + y0);
    const float y = static_cast<float>((q[0] * static_cast<double>(a1) + static_cast<double>(b1) * q[2]) + q[1]);
    const float z = static_cast<float>((q[0] * static_cast<double>(a2) + b2 * q[2]) + y0);
    q[0] = x;
    q[1] = y;
    q[2] = z;
}

// The position plus the heading times `distance` (x's product unrounded, y's
// and z's floats), into `out`.
void Ahead(const float (&p)[3], const float (&q)[3], double distance, float (&out)[3])
{
    const float y = static_cast<float>(static_cast<double>(q[1]) * distance);
    const float z = static_cast<float>(static_cast<double>(q[2]) * distance);
    out[2] = static_cast<float>(static_cast<double>(z) + p[2]);
    out[0] = static_cast<float>(static_cast<double>(q[0]) * distance + p[0]);
    out[1] = static_cast<float>(static_cast<double>(p[1]) + y);
}

// The dispatch on the mission 5: the crew at the truck's hose. The points
// start from three copies of the float 0x00B228F0; the position is the
// truck's (slot 7). With a state 0x10 stacked on the truck (its slot 57) the
// heading (slot 15) is turned a quarter either way and the target is the
// position plus it times 16 (0x00A8D45C) times 3 (0x00A85074) (x and y
// rounded once, z's 16 first); else the target is the truck's point (slot
// 143) and the heading the way to it (x and z unrounded, y a float; their
// length squares x, z, then y) normalised by 1 (0x00B0A05C) / length when it
// is longer than 1e-5 (0x00A99F20; else, or unordered, the truck's heading).
// The crew is placed (slot 16) at the position plus the heading times the
// float 0x00B21CC8, kept in the city (0x00547930) and on the map's height
// (slot 18, no null test), heading that way. Then the heading is turned an
// eighth and the point the float 0x00B21CCC along it (kept and on the map
// the same way) given to the second model (+0x110's second entry; no count
// or null test past its AddRef) by its slot 1; that model's virtual base
// gets (its slot 9 into a transform from the identity, count and flags 0)
// the matrix of the heading (SC4Matrix::FromDirection from nine copies of
// 0x00B228F0) with the flag 4 and the count one up (slot 10), and the view
// 0x00B43DD0 (no null test) is told of the model's drawable (its slot 30
// with the model's slot 0). The target goes into +0x2A8 and +0x2B8, the
// message 0xAA0F3F75 (the truck's +4, 0x00A9B2B8[+0x278], three zeros) is
// posted and the model let go of (no null test).
void DispatchHose(char* self, void* map)
{
    char* const truck = At<char*>(self, crew::kVehicle);
    const std::uintptr_t start = addr::cSC4SimFireCrew_DefaultFloat;
    float p[3], q[3], r[3], s[3], target[3];
    DefaultPoint(p, start);
    DefaultPoint(q, start);
    DefaultPoint(r, start);
    DefaultPoint(s, start);
    Virtual<std::uint32_t>(truck, cslot::kCarPosition, static_cast<float*>(p));
    if (Virtual<void*>(truck, fslot::kInState, crewdata::kHoseState)) {
        Virtual<std::uint32_t>(truck, cslot::kCarHeading, static_cast<float*>(q));
        TurnHeading(q);
        const double cell = Global<float>(addr::SC4_FloatSixteen);
        const double three = Global<float>(addr::cSC4AutomatonBase_FrontGap);
        const float x = static_cast<float>(static_cast<double>(q[0]) * cell * three);
        const float y = static_cast<float>(static_cast<double>(q[1]) * cell * three);
        const float z = static_cast<float>(static_cast<double>(q[2]) * cell);
        target[2] = static_cast<float>(static_cast<double>(z) * three + p[2]);
        target[0] = static_cast<float>(static_cast<double>(x) + p[0]);
        target[1] = static_cast<float>(static_cast<double>(p[1]) + y);
    } else {
        const float* const t = Virtual<const float*>(truck, cslot::kCarPoint);
        target[0] = t[0];
        target[1] = t[1];
        target[2] = t[2];
        const double dx = static_cast<double>(target[0]) - p[0];
        const float dy = static_cast<float>(static_cast<double>(target[1]) - p[1]);
        const double dz = static_cast<double>(target[2]) - p[2];
        q[1] = dy;
        q[0] = static_cast<float>(dx);
        q[2] = static_cast<float>(dz);
        const double length = std::sqrt((dx * dx + dz * dz) + static_cast<double>(dy) * dy);
        if (length > static_cast<double>(Global<float>(addr::SC4_FloatHundredThousandth))) {
            const double factor = static_cast<double>(Global<float>(addr::cSC4SimFireCrew_One)) / length;
            q[0] = static_cast<float>(dx * factor);
            q[1] = static_cast<float>(static_cast<double>(dy) * factor);
            q[2] = static_cast<float>(dz * factor);
        } else {
            Virtual<std::uint32_t>(truck, cslot::kCarHeading, static_cast<float*>(q));
        }
    }
    Ahead(p, q, Global<float>(addr::SC4AutomataTuning_CrewStep21CC8), r);
    CallThis<void>(addr::cSC4RiotPolice_ClampToCity, self, static_cast<float*>(r));
    OnMap(map, r);
    Virtual<std::uint32_t>(self, wslot::kPlace, static_cast<const float*>(r), static_cast<const float*>(q));
    TurnEighth(q);
    Ahead(p, q, Global<float>(addr::SC4AutomataTuning_CrewSide21CCC), s);
    CallThis<void>(addr::cSC4RiotPolice_ClampToCity, self, static_cast<float*>(s));
    OnMap(map, s);

    char* const model = At<char**>(self, base::kModels)[1];
    if (model)
        Virtual<std::uint32_t>(ModelBase(model), slot::kAddRef);
    Virtual<std::uint32_t>(model, fslot::kModelPoint, static_cast<const float*>(s));
    std::uint32_t m[9];
    for (std::uint32_t& w : m)
        w = Global<std::uint32_t>(start);
    CallCdecl<void>(addr::SC4Matrix_FromDirection, static_cast<float*>(q), static_cast<void*>(m));
    ModelTransform transform{};
    static constexpr float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(transform.m, kIdentity, sizeof kIdentity);
    transform.scale = 1.0f;
    Virtual<std::uint32_t>(ModelBase(model), fslot::kGetTransform, static_cast<void*>(&transform));
    std::memcpy(transform.m, m, sizeof m);
    transform.flags = static_cast<std::uint8_t>(transform.flags | 4);
    transform.count = static_cast<std::uint8_t>(transform.count + 1);
    Virtual<std::uint32_t>(ModelBase(model), fslot::kSetTransform, static_cast<const void*>(&transform));
    void* const view = Global<void*>(addr::SC4_View43DD0);
    Virtual<std::uint32_t>(view, fslot::kViewMove, Virtual<void*>(model, fslot::kModelDrawable));
    for (int i = 0; i < 3; ++i) {
        At<float>(self, fire::kPoint2A8 + 4 * i) = target[i];
        At<float>(self, fire::kPoint2B8 + 4 * i) = target[i];
    }
    Post(self, crewdata::kDispatchMessage, CarPart(self),
         reinterpret_cast<const std::uint32_t*>(addr::cSC4SimFireCrew_MessageWords)[At<std::uint8_t>(self, crew::kByte)],
         0u, 0u, 0u);
    Virtual<std::uint32_t>(ModelBase(model), slot::kRelease);
}

// 0x00547EF0: the city's map (slot 40) held for the while (AddRef
// null-tested, its slot 18 and the Release not); by the truck's mission
// (slot 26, the truck not null-tested) 5 the crew at the hose, else beside
// the truck (DispatchBeside with 0x00B228F0 and 0x00B21CCC; a truck without
// an occupant does not remove it); then slot 33 (0, 0) and (1.0, the float
// 0x00B21C84).
void __fastcall FireCrewDispatch(char* self, void*)
{
    static bool logged;
    FirstCall("FireCrewDispatch", logged);
    void* const map = Virtual<void*>(Global<void*>(addr::SC4_City), wslot::kCityNetwork);
    if (map)
        Virtual<std::uint32_t>(map, slot::kAddRef);
    if (Mission(self) == 5)
        DispatchHose(self, map);
    else
        DispatchBeside(self, map,
                       Beside{addr::cSC4SimFireCrew_DefaultFloat, addr::SC4AutomataTuning_CrewSide21CCC, false,
                              static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(map))});
    Virtual<std::uint32_t>(self, cslot::kFade, 0u, 0u);
    Virtual<std::uint32_t>(self, cslot::kFade, 0x3F800000u, Global<std::uint32_t>(addr::SC4AutomataTuning_Fade21C84));
    Release(map);
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

// Part ped2a.
const Detour kStates[] = {
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Init, PedestrianInit),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Frame, PedestrianFrame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Animate, PedestrianAnimate),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_StateFrame, PedestrianStateFrame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_EnterState, PedestrianEnterState),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_LeaveState, PedestrianLeaveState),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Remove, PedestrianRemove),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_DirectionTo, PedestrianDirectionTo),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_DrawFloat200, PedestrianDrawFloat200),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_NextStep, PedestrianNextStep),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Idle, PedestrianIdle),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_State4Frame, PedestrianState4Frame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_StateCFrame, PedestrianStateCFrame),
};

// Part ped2b.
const Detour kWalks[] = {
    OPENSC4_DETOUR(addr::cSC4Pedestrian_State2Frame, PedestrianState2Frame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_State3Frame, PedestrianState3Frame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_State6Frame, PedestrianState6Frame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_State8Frame, PedestrianState8Frame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_State9Frame, PedestrianState9Frame),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Bump, PedestrianBump),
    OPENSC4_DETOUR(addr::cSC4Pedestrian_Walk, PedestrianWalk),
};

// Part ped3.
const Detour kCrews[] = {
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_Init, OneShotInit),
    OPENSC4_DETOUR(addr::cSC4PedestrianOneShot_Frame, OneShotFrame),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Init, RiotPoliceInit),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Shutdown, RiotPoliceShutdown),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Frame, RiotPoliceFrame),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_SetZoomAndRotation, RiotPoliceSetZoomAndRotation),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_PostMessage, RiotPolicePostMessage),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_Dispatch, RiotPoliceDispatch),
    OPENSC4_DETOUR(addr::cSC4RiotPolice_ClampToCity, RiotPoliceClampToCity),
};

// Part ped4a.
const Detour kFireDispatch[] = {
    OPENSC4_DETOUR(addr::cSC4SimFireCrew_Dispatch, FireCrewDispatch),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kStates, sizeof kStates / sizeof kStates[0], "states") &&
           InstallList(kWalks, sizeof kWalks / sizeof kWalks[0], "walks") &&
           InstallList(kCrews, sizeof kCrews / sizeof kCrews[0], "crews") &&
           InstallList(kFireDispatch, sizeof kFireDispatch / sizeof kFireDispatch[0], "fire dispatch");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.pedestrians", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Pedestrian_Construct,
                        "Our pedestrians (the pedestrian, the one-shot, the riot police and the fire crew), the "
                        "objects: the four factories, the allocator and its release, the pool's free, the four "
                        "constructors, the two QueryInterfaces, the four deleting destructors, the fire crew's "
                        "destructor body and the four GetGZCLSIDs; the pedestrian's, the riot police's and the fire "
                        "crew's Writes and Reads; the pedestrian's slots 16, 17 and 174, the one-shot's 177 and 178 "
                        "and the crews' word (34 detours, part ped1); the pedestrian's Init, frame, animation, "
                        "states (slots 109 to 111), slots 135, 155, 158, 165 and 176 and the frames of the states 4 "
                        "and 0xC (13 detours, part ped2a); the frames of the states 2, 3, 6, 8 and 9 (the walks) and "
                        "their bump and step (7 detours, part ped2b); the one-shot's Init and frame, the riot "
                        "police's Init, Shutdown, frame, slot 74, message helper, dispatch and its clamp to the city "
                        "(9 detours, part ped3); the fire crew's dispatch (1 detour, part ped4a; "
                        "src/core/automata_pedestrians.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_pedestrians
