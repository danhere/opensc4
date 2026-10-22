// Our automaton base and cSC4Vehicle, parts vb1 to vb3 (build 641). vb1:
// the object (the vehicle's factory and pool, the two constructors, the two
// destructor bodies with their deques, the two deleting destructors,
// QueryInterface of both and the vehicle's GetGZCLSID) and the queries that
// do not run on frames by themselves (the accessors, the near and direction
// tests, the held test, the best direction, the network test and the three
// nearest automaton searches). vb2: Init and Shutdown of both (with the
// deques' release and emptying and the vehicle's models), Write and Read of
// both, the +0x204 deque's push, the managers' record list push, the
// derived classes' operator delete, the setters and the debug texts. vb3:
// the base's movement on frames (SetDestination, the moves along a path and
// straight to the destination, the path's walk and sideways offset,
// entering an occupant, placing on the network, the next route cell, the
// room ahead, the side weights and the route's pop_front). The
// class map is in src/core/automata_vehicle.h and the survey in
// docs/automata.md, "Part vb". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Group, off by default:
//   simulators.automata.vehicle: the 36 detours in kObject (vb1), the 30
//   in kLife (vb2) and the 13 in kMove (vb3) below.
//
// What it does. cSC4Vehicle's factory takes a 0x2B8-byte block from the
// vehicle pool (0x00B0A0F8; other sizes go to SC4::operator_new) and runs
// the constructor with four zero words. The constructor runs the automaton
// base's (which runs the automata core's 0x005140A0), sets the six vtables,
// zeroes its fields, makes its two deques, sets the occupant type
// 0xC7CC75F1 and draws a time (0x00B21BCC..0x00B21BD0 from the generator
// 0x00B21D78, over 3600) into +0x80 and, when it changed, into the smoothed
// value at +0x1A0. The destructor bodies release the deques' objects and the
// two references and tail-jump to the core's destructor 0x00513E60; the
// deleting destructors free the base through SC4::operator_delete and the
// vehicle back to its pool.
//
// Oddities kept: the vehicle's QueryInterface leaves the out pointer alone
// for other interfaces (the base's sets it to null first); the deleting
// destructor of the base frees without a null test; NearestAutomaton keeps
// its best distance in its first argument's stack slot, which starts at 0,
// so only automata within the margin count, and the ties go to the later
// one; NearestOnNetwork measures in x and z only (the y difference is the
// float at 0x00A81054) and, when the key is the automaton's own occupant,
// starts at its own node; DirectionTo multiplies the unrounded z difference
// by its float copy; BestDirection keeps its best dot product as a float.
//
// Stack leftovers the game passes on (ours passes zeros): BestDirection's
// direction argument to the network's slot 0xD4 is a byte with three bytes
// of the caller's stack above it; NetworkTest's third argument to the
// network part's slot 0x90 and second to 0x00B43D54's slot 0x3C are bytes
// (+0x1F5 and +0x1F4) with three bytes of stack above them. The bool
// answers: the game sets AL only in QueryInterface, IsNearPoint,
// IsHeld, NetworkTest and GetByte1E8, leaving the rest of EAX as it was.
//
// vb2's oddities kept: cSC4Vehicle's Write writes the core's default point
// 0x00B09A04 where +0x270 would be, and its Read drops what it reads of the
// +0x27C deque (the object its loop would release is a local it zeroes) and
// of the second point; the base's Read takes +0x1E4 and +0x1FC through one
// local word (a failed read leaves the earlier value in the second); Write
// calls slot 0xEC of each +0x27C element's object without a null test;
// slot 149 sets the flag 8 with the whole argument word but compares its
// byte. Locals the game leaves as stack garbage before a read that may fail
// (the version words, Read's words and cells, Write's two key words) are
// zero in ours, and the allocator byte the debug texts pass to
// SC4String::AssignRange and AppendRange is a zero byte of ours. AL only in
// the game: both Inits, both Writes and Reads (and the rest of EAX is the
// callee's), slots 172 and 184 (AL 0); ours zero-extends. cSC4Vehicle's
// Shutdown has EAX's upper bytes zero in the game too; the base's passes the
// core's EAX on, as do slots 124, 135 and 149.
//
// Floats: 53-bit doubles, as the game's x87 runs. The distances keep the
// asm's order ((dz * dz + dy * dy) + dx * dx); floats are stored where the
// game stores them (the best distances, the draw, the direction's factor).
// Compares keep the asm's handling of unordered values.
//
// vb3's oddities kept: SetDestination and slot 130 take the y difference as
// the destination's y less itself (0 but for infinities and NaN); slot 170
// moves a share of the opposite side's weight that is at least 100 (the
// larger of the weight and 100), normalises +0xA4 without a zero test and
// passes the network argument's own stack slot as BestDirection's out byte
// (its upper three bytes the pointer's, which then go to the network's slot
// 0xD4); slot 133 compares the index unsigned (a path of no points walks
// from a huge limit), leaves leftover alone when it ends inside the path
// and, past its end, moves out by the whole step along the last segment;
// slot 157 does not test +0x1F8 for null; slot 129 calls city slot 40 and
// slot 174 and drops their answers. Stack garbage the game passes or reads
// back (ours zeros): the byte arguments with three bytes above them
// (slot 131's key byte 1 to the part's slot 5 and the map's slot 19, the
// side to the parts' slots 5 and 9, slot 170's row to slot 0xD4), and the
// out words and cells its callees always write (0x0054E6F0's table, slot
// 134's cells and out word, slot 142's out word). AL only in the game:
// slots 129 to 131, 133, 134, 142 and 170; ours zero-extends.
//
// Still the game's, at their addresses: the automata core (constructor
// 0x005140A0, destructor 0x00513E60, QueryInterface 0x00505D80, AddRef,
// Release and the smoothed value 0x004FA960); the adjustor thunks of the six
// parts; the two plain jumps 0x0055B6C0 and 0x0055B740; the accessors folded
// with other classes (0x00504F70 and 0x00505100 with cSC4City's, 0x00520080
// with four others); the deques' own code (0x008EB800, 0x0049EB90,
// 0x006DA8D0, SC4RouteDeque), the pool's (SC4EdgePool::Grow,
// SC4Pool::ReleaseBlock, SC4Pool::ReopenBlock) and the generator's.
#include "core/automata_vehicle.h"

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

namespace opensc4::automata_vehicle {
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
    log::Write("automata vehicle: first %s", name);
}

void Release(void* object) { Virtual<std::uint32_t>(object, slot::kRelease); }

// The distance of two points as the asm adds it: (dz * dz + dy * dy) + dx * dx.
double Length(double dx, double dy, double dz) { return std::sqrt((dz * dz + dy * dy) + dx * dx); }

// A deque iterator, passed by value to the range releases.
struct DequeIterator {
    char* current;
    char* first;
    char* last;
    char** node;
};

DequeIterator IteratorAt(char* deque, std::size_t at)
{
    return {At<char*>(deque, at + deque::kCurrent), At<char*>(deque, at + deque::kFirst),
            At<char*>(deque, at + deque::kLast), At<char**>(deque, at + deque::kNode)};
}

// ====================================================================================
// The object

// 0x0055B970, cdecl (size): a 0x2B8-byte block from the vehicle pool
// (0x00B0A0F8, its page at +4: the free head at +8, the count at +0xC), a
// new page from SC4EdgePool::Grow when the page has none; other sizes from
// SC4::operator_new.
void* __cdecl PoolAllocate(std::size_t size)
{
    static bool logged;
    FirstCall("PoolAllocate", logged);
    if (size != kVehicleSize)
        return CallCdecl<void*>(addr::SC4_operator_new, size);
    char* block = At<char*>(Global<char*>(addr::cSC4Vehicle_Pool + 4), 8);
    if (!block)
        block = CallThis<char*>(addr::SC4EdgePool_Grow, reinterpret_cast<void*>(addr::cSC4Vehicle_Pool));
    At<char*>(Global<char*>(addr::cSC4Vehicle_Pool + 4), 8) = At<char*>(block, 0);
    ++At<std::uint32_t>(Global<char*>(addr::cSC4Vehicle_Pool + 4), 0xC);
    return block;
}

// 0x0055F600, on the pool (block), ret 4: the block back onto its page's
// free list (the page is the block's address & 0xFFFF0000); the page let go
// at count 0 (SC4Pool::ReleaseBlock) and reopened at 0x40000046
// (SC4Pool::ReopenBlock).
void __fastcall PoolFree(void* pool, void*, char* block)
{
    static bool logged;
    FirstCall("PoolFree", logged);
    if (!block)
        return;
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(block) & 0xFFFF0000u);
    At<char*>(block, 0) = At<char*>(page, 8);
    At<char*>(page, 8) = block;
    const std::uint32_t count = --At<std::uint32_t>(page, 0xC);
    if (count == 0)
        CallThis<void>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (count == 0x40000046u)
        CallThis<void>(addr::SC4Pool_ReopenBlock, pool, page);
}

// 0x005544D0, the automaton base's constructor (four words for the core),
// ret 0x10.
char* __fastcall BaseConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                               std::uint32_t d)
{
    static bool logged;
    FirstCall("BaseConstruct", logged);
    CallThis<char*>(addr::cSC4AutomatonCore_Construct, self, a, b, c, d);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomatonBase_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4AutomatonBase_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomatonBase_Vtable8;
    At<std::uintptr_t>(self, field::kVtableC) = addr::cSC4AutomatonBase_VtableC;
    At<std::uintptr_t>(self, field::kVtable20) = addr::cSC4AutomatonBase_Vtable20;
    At<std::uintptr_t>(self, field::kVtable38) = addr::cSC4AutomatonBase_Vtable38;
    At<std::uint8_t>(self, field::kByte1E8) = 0;
    At<std::uint32_t>(self, field::kOccupant) = 0;
    At<std::uint32_t>(self, field::kNetwork) = 0;
    At<std::uint32_t>(self, field::k1F8) = 0;
    At<std::uint32_t>(self, field::k1FC) = 0;
    At<std::uint32_t>(self, field::kFloat200) = 0;
    std::memset(self + field::kDeque204, 0, deque::kSize);
    CallThis<void>(addr::SC4AutomatonDeque204_Init, self + field::kDeque204, 0u);
    std::memset(self + field::kRoute, 0, deque::kSize);
    CallThis<void>(addr::SC4RouteDeque_Init, self + field::kRoute, 0u);
    At<std::uint8_t>(self, field::kFlags33) |= 0x10;
    At<std::uint32_t>(self, field::kBytes1F4) = 0xFFFF;
    At<std::uint32_t>(self, field::kState) = 7;
    return self;
}

// 0x005628F0, cSC4Vehicle's constructor (the same four words), ret 0x10.
char* __fastcall VehicleConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                  std::uint32_t d)
{
    static bool logged;
    FirstCall("VehicleConstruct", logged);
    CallThis<char*>(addr::cSC4AutomatonBase_Construct, self, a, b, c, d);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4Vehicle_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4Vehicle_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4Vehicle_Vtable8;
    At<std::uintptr_t>(self, field::kVtableC) = addr::cSC4Vehicle_VtableC;
    At<std::uintptr_t>(self, field::kVtable20) = addr::cSC4Vehicle_Vtable20;
    At<std::uintptr_t>(self, field::kVtable38) = addr::cSC4Vehicle_Vtable38;
    for (std::size_t at = field::kVehicle254; at < field::kPoint264; at += 4)
        At<std::uint32_t>(self, at) = 0;
    for (const std::size_t at : {field::kPoint264, field::kPoint270})
        for (std::size_t i = 0; i < 12; i += 4)
            At<std::uint32_t>(self, at + i) = Global<std::uint32_t>(addr::cSC4AutomatonCore_DefaultPoint + i);
    std::memset(self + field::kDeque27C, 0, deque::kSize);
    CallThis<void>(addr::SC4VehicleDeque27C_Init, self + field::kDeque27C, 0u);
    At<std::uint8_t>(self, field::kByte2B4) = 0;
    At<std::uint32_t>(self, field::kType) = kVehicleType;
    for (std::size_t at = field::kVehicle2A4; at < field::kByte2B4; at += 4)
        At<std::uint32_t>(self, at) = 0;
    const std::uint32_t drawn =
        CallThis<std::uint32_t>(addr::cRZRandomSuper_Between, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random),
                                Global<std::uint32_t>(addr::cSC4Vehicle_DrawLow),
                                Global<std::uint32_t>(addr::cSC4Vehicle_DrawHigh));
    // FILD of the answer as a signed int, times the float 1/3600.
    const float value = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(drawn)) *
                                           static_cast<double>(Global<float>(addr::cSC4Vehicle_DrawScale)));
    At<float>(self, field::kDraw) = value;
    // FUCOMPP: equal skips; different or unordered sets the smoothed value.
    if (!(value == At<float>(self, field::kSmoothed + 4))) {
        const double now = CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, self + field::kSmoothed);
        At<float>(self, field::kSmoothed) = static_cast<float>(now);
        At<float>(self, field::kSmoothed + 4) = value;
        At<std::uint32_t>(self, field::kSmoothed + 8) = 0;
        At<std::uint32_t>(self, field::kSmoothed + 0xC) = 0;
    }
    return self;
}

// 0x005148E0, cSC4Vehicle's GZCOM factory (cdecl): a pool block and the
// constructor with four zero words; answers the object itself.
void* __cdecl VehicleCreate()
{
    static bool logged;
    FirstCall("VehicleCreate", logged);
    void* const block = CallCdecl<void*>(addr::cSC4Vehicle_PoolAllocate, static_cast<std::size_t>(kVehicleSize));
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4Vehicle_Construct, block, 0u, 0u, 0u, 0u);
}

// 0x00504CB0, cdecl (start, finish, allocator): each 0xC-byte element's
// object released (blocks of 0x78 bytes).
void __cdecl ReleaseRange204(DequeIterator start, DequeIterator finish, void*)
{
    static bool logged;
    FirstCall("ReleaseRange204", logged);
    char* current = start.current;
    char* last = start.last;
    char** node = start.node;
    while (current != finish.current) {
        if (void* const object = At<void*>(current, 0))
            Release(object);
        current += 0xC;
        if (current == last) {
            ++node;
            current = *node;
            last = current + 0x78;
        }
    }
}

// 0x00504C70, the same for 8-byte elements (blocks of 0x80 bytes).
void __cdecl ReleaseRange27C(DequeIterator start, DequeIterator finish, void*)
{
    static bool logged;
    FirstCall("ReleaseRange27C", logged);
    char* current = start.current;
    char* last = start.last;
    char** node = start.node;
    while (current != finish.current) {
        if (void* const object = At<void*>(current, 0))
            Release(object);
        current += 8;
        if (current == last) {
            ++node;
            current = *node;
            last = current + 0x80;
        }
    }
}

// The deques' destruction: the range released, the blocks freed
// (0x006DA8D0 from the start's node to one past the finish's), the map
// freed. The fields are left as they were.
void DestroyDeque(char* d, std::uintptr_t release)
{
    char allocator = 0;
    CallCdecl<void>(release, IteratorAt(d, deque::kStart), IteratorAt(d, deque::kFinish),
                    static_cast<void*>(&allocator));
    if (At<void*>(d, deque::kMap)) {
        CallThis<void>(addr::SC4Deque_FreeBlocks, d, At<char**>(d, deque::kStart + deque::kNode),
                       At<char**>(d, deque::kFinish + deque::kNode) + 1);
        if (void* const map = At<void*>(d, deque::kMap))
            CallCdecl<void>(addr::SC4_Free, map);
    }
}

// 0x00504D90, on the +0x204 deque.
void __fastcall DestroyDeque204(char* d, void*)
{
    static bool logged;
    FirstCall("DestroyDeque204", logged);
    DestroyDeque(d, addr::cSC4AutomatonBase_ReleaseRange204);
}

// 0x00504CF0, on cSC4Vehicle's +0x27C deque.
void __fastcall DestroyDeque27C(char* d, void*)
{
    static bool logged;
    FirstCall("DestroyDeque27C", logged);
    DestroyDeque(d, addr::cSC4Vehicle_ReleaseRange27C);
}

// The base's part of both destructor bodies, then the core's (a tail jump in
// the game).
void* DestroyBasePart(char* self)
{
    CallThis<void>(addr::SC4RouteDeque_Destroy, self + field::kRoute);
    CallThis<void>(addr::cSC4AutomatonBase_DestroyDeque204, self + field::kDeque204);
    if (void* const network = At<void*>(self, field::kNetwork))
        Release(network);
    if (void* const occupant = At<void*>(self, field::kOccupant))
        Release(occupant);
    return CallThis<void*>(addr::cSC4AutomatonCore_Destroy, self);
}

// 0x00504E30, the base's destructor body.
void* __fastcall BaseDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("BaseDestroy", logged);
    return DestroyBasePart(self);
}

// 0x00504E70, cSC4Vehicle's destructor body: the +0x27C deque, the objects
// at +0x25C and +0x258 released through their virtual base (the vbtable at
// +4, its second word the offset from +4), then the base's part inline.
void* __fastcall VehicleDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("VehicleDestroy", logged);
    CallThis<void>(addr::cSC4Vehicle_DestroyDeque27C, self + field::kDeque27C);
    for (const std::size_t at : {field::kVirtual25C, field::kVirtual258})
        if (char* const object = At<char*>(self, at)) {
            const std::int32_t offset = At<std::int32_t>(At<char*>(object, 4), 4);
            Release(object + 4 + offset);
        }
    return DestroyBasePart(self);
}

// 0x005552F0, the base's deleting destructor (flags), ret 4.
char* __fastcall BaseDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("BaseDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4AutomatonBase_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// 0x00562A90, cSC4Vehicle's deleting destructor (flags), ret 4: the block
// back to the vehicle pool.
char* __fastcall VehicleDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("VehicleDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
    if ((flags & 1) && self)
        CallThis<void>(addr::cSC4Vehicle_PoolFree, reinterpret_cast<void*>(addr::cSC4Vehicle_Pool), self);
    return self;
}

// 0x0054D7F0, the base's slot 0 (iid, out), ret 8: out null first;
// 0x8B99EE7C answers the object (AddRef); the rest the core's 0x00505D80.
std::uint8_t __fastcall BaseQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("BaseQueryInterface", logged);
    *out = nullptr;
    if (iid == kBaseInterfaceId) {
        *out = self;
        Virtual<std::uint32_t>(self, slot::kAddRef);
        return 1;
    }
    return CallThis<std::uint8_t>(addr::cSC4AutomatonCore_QueryInterface, self, iid, out);
}

// 0x0055B660, cSC4Vehicle's slot 0, ret 8: its class ID answers the object;
// the rest the base's.
std::uint8_t __fastcall VehicleQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("VehicleQueryInterface", logged);
    if (iid == kVehicleClassId) {
        *out = self;
        Virtual<std::uint32_t>(self, slot::kAddRef);
        return 1;
    }
    return CallThis<std::uint8_t>(addr::cSC4AutomatonBase_QueryInterface, self, iid, out);
}

// 0x00532240, the serializable part's slot 5.
std::uint32_t __fastcall VehicleGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("VehicleGetGZCLSID", logged);
    return kVehicleClassId;
}

// ====================================================================================
// The accessors

// 0x00504F80, slot 138: the float +0x200.
float __fastcall GetFloat200(char* self, void*)
{
    static bool logged;
    FirstCall("GetFloat200", logged);
    return At<float>(self, field::kFloat200);
}

// 0x00504F90, slot 144: the route deque's address.
void* __fastcall GetRoute(char* self, void*)
{
    static bool logged;
    FirstCall("GetRoute", logged);
    return self + field::kRoute;
}

// 0x00504FA0, slot 150: the automata flag 8 (slot 31), its answer as it is.
std::uint32_t __fastcall HasFlag8(char* self, void*)
{
    static bool logged;
    FirstCall("HasFlag8", logged);
    return Virtual<std::uint32_t>(self, slot::kCheckFlag, 8u);
}

// 0x00504FB0, slot 153: the byte +0x1E8.
std::uint8_t __fastcall GetByte1E8(char* self, void*)
{
    static bool logged;
    FirstCall("GetByte1E8", logged);
    return At<std::uint8_t>(self, field::kByte1E8);
}

// 0x00504FD0, slot 156: the +0x204 deque's address.
void* __fastcall GetDeque204(char* self, void*)
{
    static bool logged;
    FirstCall("GetDeque204", logged);
    return self + field::kDeque204;
}

// 0x00504FE0, slot 175: the static byte 0x00B21D28, zero-extended.
std::uint32_t __fastcall GetStaticByte(void*, void*)
{
    static bool logged;
    FirstCall("GetStaticByte", logged);
    return Global<std::uint8_t>(addr::cSC4AutomatonBase_StaticByte);
}

// 0x00546020, the base's slot 140: the position's address (+0x40).
void* __fastcall GetPositionAddress(char* self, void*)
{
    static bool logged;
    FirstCall("GetPositionAddress", logged);
    return self + field::kPosition;
}

// 0x00504FF0, cSC4Vehicle's slot 140: +0x264's address.
void* __fastcall GetPoint264(char* self, void*)
{
    static bool logged;
    FirstCall("GetPoint264", logged);
    return self + field::kPoint264;
}

// 0x00505000, cSC4Vehicle's slot 179: +0x270's address.
void* __fastcall GetPoint270(char* self, void*)
{
    static bool logged;
    FirstCall("GetPoint270", logged);
    return self + field::kPoint270;
}

// 0x00505010, cSC4Vehicle's slot 183: the +0x27C deque's address.
void* __fastcall GetDeque27C(char* self, void*)
{
    static bool logged;
    FirstCall("GetDeque27C", logged);
    return self + field::kDeque27C;
}

// 0x0055BF60, cSC4Vehicle's slot 174: the city's vehicle manager (city slot
// 43; a tail jump in the game). This not read.
void* __fastcall GetManager(void*, void*)
{
    static bool logged;
    FirstCall("GetManager", logged);
    void* const city = Global<void*>(addr::SC4_City);
    return Virtual<void*>(city, kCityVehicleManager);
}

// 0x0055F900, on the +0x27C deque: its last element (the finish's current
// minus 8, from the block before when the finish is at its block's start).
char* __fastcall DequeBack27C(char* d, void*)
{
    static bool logged;
    FirstCall("DequeBack27C", logged);
    char* current = At<char*>(d, deque::kFinish + deque::kCurrent);
    if (current == At<char*>(d, deque::kFinish + deque::kFirst))
        current = At<char**>(d, deque::kFinish + deque::kNode)[-1] + 0x80;
    return current - 8;
}

// ====================================================================================
// The queries

// 0x0054DAB0, slot 146: whether the second point (slot 143) is within
// 16 x the byte 0x00B21CB8 of the position; false when unordered.
std::uint8_t __fastcall IsNearPoint(char* self, void*)
{
    static bool logged;
    FirstCall("IsNearPoint", logged);
    const float* const point = Virtual<const float*>(self, slot::kPoint);
    const float* const position = &At<float>(self, field::kPosition);
    const double distance = Length(static_cast<double>(point[0]) - position[0],
                                   static_cast<double>(point[1]) - position[1],
                                   static_cast<double>(point[2]) - position[2]);
    const std::int32_t limit = static_cast<std::int32_t>(Global<std::uint8_t>(addr::cSC4AutomatonBase_NearCells)) << 4;
    return static_cast<double>(limit) >= distance ? 1 : 0;
}

// 0x0054EA80, slot 155 (point, out), ret 8: the unit vector from the
// position to the point; (0, 0, 1) when the distance is at most 1e-5
// (0x00A9BCD8).
void __fastcall DirectionTo(char* self, void*, const float* point, float* out)
{
    static bool logged;
    FirstCall("DirectionTo", logged);
    const float* const position = &At<float>(self, field::kPosition);
    const double dx = static_cast<double>(point[0]) - position[0];
    const float dy = static_cast<float>(static_cast<double>(point[1]) - position[1]);
    const double dz = static_cast<double>(point[2]) - position[2];
    const float dzf = static_cast<float>(dz);
    const double length = std::sqrt((dz * dzf + static_cast<double>(dy) * dy) + dx * dx);
    const double epsilon = Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon);
    if (length < epsilon || length == epsilon) {
        out[1] = 0.0f;
        out[0] = 0.0f;
        out[2] = 1.0f;
        return;
    }
    const float factor = static_cast<float>(static_cast<double>(Global<float>(addr::cSC4AutomatonBase_One)) / length);
    out[0] = static_cast<float>(dx * factor);
    out[1] = static_cast<float>(static_cast<double>(dy) * factor);
    out[2] = static_cast<float>(static_cast<double>(dzf) * factor);
}

// 0x0054EA30: true with the byte +0x1E8, the state 7 or 8, the automata flag
// 0x200000 or an occupant (+0x1EC) in group 2 (its slot 0x58).
std::uint8_t __fastcall IsHeld(char* self, void*)
{
    static bool logged;
    FirstCall("IsHeld", logged);
    if (At<std::uint8_t>(self, field::kByte1E8))
        return 1;
    const std::uint32_t state = At<std::uint32_t>(self, field::kState);
    if (state == 7 || state == 8)
        return 1;
    if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x200000u))
        return 1;
    void* const occupant = At<void*>(self, field::kOccupant);
    if (occupant && Virtual<std::uint8_t>(occupant, kOccupantGroupSlot, 2u))
        return 1;
    return 0;
}

// 0x0054D8D0, slot 171 (network, direction, out, check), ret 0x10: of the
// four sides (-x, -z, +x, +z) the one whose unit vector has the largest dot
// product with the direction (above -2), with check only sides the network
// answers for (its slot 0xD4 with the kind, 0xFF and the side); the side
// into out (0xFF for none); 1 when there is one and its dot product is at
// least 0.6 (0x00A9809C).
std::uint32_t __fastcall BestDirection(char* self, void*, void* network, const float* direction, std::uint8_t* out,
                                       std::uint8_t check)
{
    static bool logged;
    FirstCall("BestDirection", logged);
    static constexpr float kSides[4][3] = {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    float best = -2.0f;
    *out = 0xFF;
    for (std::uint8_t side = 0; side < 4; ++side) {
        const float* const v = kSides[side];
        const double dot = (static_cast<double>(v[0]) * direction[0] + static_cast<double>(v[2]) * direction[2]) +
                           static_cast<double>(direction[1]) * v[1];
        const float dotf = static_cast<float>(dot);
        if (!(dot > best))
            continue;
        if (check) {
            const std::uint32_t kind = At<std::uint8_t>(self, field::kKind1F7) & 0xFu;
            if (!Virtual<std::uint8_t>(network, kNeighbourSlot, kind, 0xFFu, static_cast<std::uint32_t>(side)))
                continue;
        }
        *out = side;
        best = dotf;
    }
    if (*out == 0xFF)
        return 0;
    if (!(best >= Global<float>(addr::SC4_Float0_6)))
        return 0;
    return 1;
}

// 0x0054EB90, slot 152 (a), ret 4: false without the network part (+0x1F0);
// its slot 0x90 (a, kind, +0x1F5, 0xFF, 0); when that says no, the occupant
// beside +0x1EC on side +0x1F4 (0x00B43D54's slot 0x3C), its network part
// (slot 0xC4) and its slot 0x90 for the opposite side.
std::uint8_t __fastcall NetworkTest(char* self, void*, std::uint32_t a)
{
    static bool logged;
    FirstCall("NetworkTest", logged);
    void* const network = At<void*>(self, field::kNetwork);
    if (!network)
        return 0;
    const std::uint32_t kind = At<std::uint8_t>(self, field::kKind1F7) & 0xFu;
    const std::uint8_t side = At<std::uint8_t>(self, field::kBytes1F4);
    const std::uint8_t entry = At<std::uint8_t>(self, field::kByte1F5);
    const std::uint8_t answer =
        Virtual<std::uint8_t>(network, kNetworkPathSlot, a, kind, static_cast<std::uint32_t>(entry), 0xFFu, 0u);
    if (answer)
        return answer;
    void* const service = Global<void*>(addr::SC4_Service43D54);
    void** const beside = Virtual<void**>(service, kServiceNeighbour, At<void*>(self, field::kOccupant),
                                          static_cast<std::uint32_t>(side), 1u, kind);
    if (!beside || !*beside)
        return answer;
    void* const part = Virtual<void*>(*beside, kNetworkPartSlot);
    if (!part)
        return answer;
    return Virtual<std::uint8_t>(part, kNetworkPathSlot, a, kind, (static_cast<std::uint32_t>(side) + 2u) & 3u, 0xFFu,
                                 0u);
}

// 0x0050AAF0, on an automata manager (automaton, margin), ret 8: of the
// manager's automata (+0x44) other than the automaton and that it can meet
// (its slot 73), the one whose distance less both radii and the margin is
// smallest, counting only those at or below 0 (the best starts at 0 in the
// first argument's slot); ties go to the later one. Null for none.
void* __fastcall NearestAutomaton(char* manager, void*, char* automaton, float margin)
{
    static bool logged;
    FirstCall("NearestAutomaton", logged);
    char* found = nullptr;
    const float radius = static_cast<float>(Virtual<double>(automaton, slot::kGetRadius));
    const float start = Global<float>(addr::cSC4AutomataManager_PointInit);
    float own[3] = {start, start, start};
    float other[3] = {start, start, start};
    float best = 0.0f;
    Virtual<void>(automaton, slot::kGetPosition, static_cast<float*>(own));
    char* const head = At<char*>(manager, kManagerAutomata);
    for (char* node = At<char*>(head, 0); node != At<char*>(manager, kManagerAutomata); node = At<char*>(node, 0)) {
        char* const candidate = At<char*>(node, 8);
        if (candidate == automaton || !Virtual<std::uint8_t>(automaton, slot::kCanMeet, candidate))
            continue;
        const float otherRadius = static_cast<float>(Virtual<double>(candidate, slot::kGetRadius));
        Virtual<void>(candidate, slot::kGetPosition, static_cast<float*>(other));
        const double distance =
            Length(static_cast<double>(other[0]) - own[0], static_cast<double>(other[1]) - own[1],
                   static_cast<double>(other[2]) - own[2]) -
            ((static_cast<double>(otherRadius) + radius) + margin);
        if (distance < best || distance == best) {
            best = static_cast<float>(distance);
            found = candidate;
        }
    }
    return found;
}

// 0x005573C0, cSC4Vehicle's slot 123 (margin), ret 4: NearestAutomaton on
// the city's vehicle manager.
void* __fastcall NearestVehicle(char* self, void*, float margin)
{
    static bool logged;
    FirstCall("NearestVehicle", logged);
    void* const city = Global<void*>(addr::SC4_City);
    char* const manager = Virtual<char*>(city, kCityVehicleManager);
    return CallThis<void*>(addr::cSC4AutomataManager_NearestAutomaton, manager, self, margin);
}

// 0x0054FC30 (key, value, mask1, mask2), ret 0x10: the list of 0x00B43D54's
// slot 0x40 for the key (+0x1C: nodes, the automaton at +8; from the
// automaton's own node when the key is its occupant +0x1EC); of those not
// gone (slot 85) that it can meet (slot 73) and that have the automata flag
// 0x2000 or groups (slot 136) matching the value under either mask, the one
// nearest in x and z (strictly below the best, which starts at FLT_MAX).
void* __fastcall NearestOnNetwork(char* self, void*, std::uint32_t key, std::uint32_t value, std::uint32_t mask1,
                                  std::uint32_t mask2)
{
    static bool logged;
    FirstCall("NearestOnNetwork", logged);
    void* const service = Global<void*>(addr::SC4_Service43D54);
    char* const list = Virtual<char*>(service, kServiceList, key);
    if (!list)
        return nullptr;
    float best = 3.4028235e38f; // 0x7F7FFFFF
    const float* const p = Virtual<const float*>(self, slot::kPosition);
    const float own[3] = {p[0], p[1], p[2]};
    char* const head = At<char*>(list, 0x1C);
    char* node;
    if (key == At<std::uint32_t>(self, field::kOccupant)) {
        node = At<char*>(head, 0);
        while (node != head && At<char*>(node, 8) != self)
            node = At<char*>(node, 0);
    } else {
        node = At<char*>(head, 0);
    }
    if (node == head)
        return nullptr;
    char* found = nullptr;
    do {
        char* const candidate = At<char*>(node, 8);
        bool take = false;
        if (!Virtual<std::uint8_t>(candidate, slot::kIsGone) && Virtual<std::uint8_t>(self, slot::kCanMeet, candidate)) {
            if (Virtual<std::uint8_t>(candidate, slot::kCheckFlag, 0x2000u))
                take = true;
            else if ((Virtual<std::uint32_t>(candidate, slot::kGroups) & mask1) == (value & mask1))
                take = true;
            else if ((Virtual<std::uint32_t>(candidate, slot::kGroups) & mask2) == (value & mask2))
                take = true;
        }
        if (take) {
            const float start = Global<float>(addr::cSC4AutomatonBase_PointInit);
            float other[3] = {start, start, start};
            Virtual<void>(candidate, slot::kGetPosition, static_cast<float*>(other));
            const double dy = Global<float>(addr::SC4_FloatZero);
            const double distance =
                Length(static_cast<double>(other[0]) - own[0], dy, static_cast<double>(other[2]) - own[2]);
            if (distance < best) {
                best = static_cast<float>(distance);
                found = candidate;
            }
        }
        node = At<char*>(node, 0);
    } while (node != At<char*>(list, 0x1C));
    return found;
}

// ====================================================================================
// Part vb2: the life cycle, the save, the setters and the debug texts

void AddRef(void* object) { Virtual<std::uint32_t>(object, slot::kAddRef); }

// An object held through a virtual base (+4 its vbtable, whose second word is
// the offset from +4): released there.
void ReleaseVirtual(char* object) { Release(object + 4 + At<std::int32_t>(At<char*>(object, 4), 4)); }

// The object pointer `self + at` released and cleared (the field first).
void ClearReference(char* self, std::size_t at)
{
    if (void* const object = At<void*>(self, at)) {
        At<void*>(self, at) = nullptr;
        Release(object);
    }
}

// The elements of a deque from start to finish: each element's object
// cleared and released (the finish re-read each step, as the asm does).
void ReleaseElements(char* d, std::size_t size, std::size_t block)
{
    char* current = At<char*>(d, deque::kStart + deque::kCurrent);
    char* last = At<char*>(d, deque::kStart + deque::kLast);
    char** node = At<char**>(d, deque::kStart + deque::kNode);
    while (current != At<char*>(d, deque::kFinish + deque::kCurrent)) {
        ClearReference(current, 0);
        current += size;
        if (current == last) {
            ++node;
            current = *node;
            last = current + block;
        }
    }
}

// A deque emptied (the STL clear): the whole blocks between the start's and
// the finish's released (the objects not cleared) and freed, the start's
// block from its current and the finish's to its current released, the
// finish's block freed; the finish set to the start.
void ClearDeque(char* d, std::size_t size, std::size_t block)
{
    auto releaseRange = [size](char* from, char* to) {
        for (char* at = from; at != to; at += size)
            if (void* const object = At<void*>(at, 0))
                Release(object);
    };
    for (char** node = At<char**>(d, deque::kStart + deque::kNode) + 1;
         node < At<char**>(d, deque::kFinish + deque::kNode); ++node) {
        releaseRange(*node, *node + block);
        if (*node)
            CallCdecl<void>(addr::SC4_Free, *node);
    }
    if (At<char**>(d, deque::kStart + deque::kNode) != At<char**>(d, deque::kFinish + deque::kNode)) {
        releaseRange(At<char*>(d, deque::kStart + deque::kCurrent), At<char*>(d, deque::kStart + deque::kLast));
        releaseRange(At<char*>(d, deque::kFinish + deque::kFirst), At<char*>(d, deque::kFinish + deque::kCurrent));
        if (char* const first = At<char*>(d, deque::kFinish + deque::kFirst))
            CallCdecl<void>(addr::SC4_Free, first);
    } else {
        releaseRange(At<char*>(d, deque::kStart + deque::kCurrent), At<char*>(d, deque::kFinish + deque::kCurrent));
    }
    for (std::size_t i = 0; i < deque::kFinish; i += 4)
        At<std::uint32_t>(d, deque::kFinish + i) = At<std::uint32_t>(d, deque::kStart + i);
}

std::int32_t Difference(const void* a, const void* b)
{
    return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(a) - reinterpret_cast<std::uintptr_t>(b));
}

// The route deque's size as the asm counts it: (the blocks between less
// one) x 42, plus the finish's block to its current and the start's from its
// current, each a signed division by 3.
std::uint32_t RouteSize(char* d)
{
    const std::int32_t nodes =
        Difference(At<char**>(d, deque::kFinish + deque::kNode), At<char**>(d, deque::kStart + deque::kNode)) >> 2;
    const std::int32_t inFinish =
        Difference(At<char*>(d, deque::kFinish + deque::kCurrent), At<char*>(d, deque::kFinish + deque::kFirst)) / 3;
    const std::int32_t inStart =
        Difference(At<char*>(d, deque::kStart + deque::kLast), At<char*>(d, deque::kStart + deque::kCurrent)) / 3;
    return static_cast<std::uint32_t>(nodes - 1) * 0x2Au + static_cast<std::uint32_t>(inFinish) +
           static_cast<std::uint32_t>(inStart);
}

// The +0x27C deque's size: as RouteSize with 16 per block and shifts by 3
// (SAR) for the divisions.
std::uint32_t QueueSize(char* d)
{
    const std::int32_t nodes =
        Difference(At<char**>(d, deque::kFinish + deque::kNode), At<char**>(d, deque::kStart + deque::kNode)) >> 2;
    const std::int32_t inFinish =
        Difference(At<char*>(d, deque::kFinish + deque::kCurrent), At<char*>(d, deque::kFinish + deque::kFirst)) >> 3;
    const std::int32_t inStart =
        Difference(At<char*>(d, deque::kStart + deque::kLast), At<char*>(d, deque::kStart + deque::kCurrent)) >> 3;
    return (static_cast<std::uint32_t>(nodes - 1) << 4) + static_cast<std::uint32_t>(inFinish) +
           static_cast<std::uint32_t>(inStart);
}

// A string of the game (cRZBaseString over an STL buffer, 0x14 bytes).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t word10;
};

// The game's construction of an empty string: an 8-byte buffer.
void MakeString(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = CallCdecl<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    buffer[0] = 0;
    text.vtable = addr::SC4String_Vtable;
    text.word10 = 0;
}

void FreeString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        CallCdecl<void>(addr::SC4_Free, text.begin);
}

// A function-local static string of the game's: made on first use (the
// guard's bit 1), its destructor given to SC4::atexit.
GameString* StaticString(std::uintptr_t at, std::uintptr_t guard, std::uintptr_t atExit)
{
    if (!(Global<std::uint8_t>(guard) & 1)) {
        *reinterpret_cast<std::uint32_t*>(guard) |= 1;
        MakeString(*reinterpret_cast<GameString*>(at));
        CallCdecl<int>(addr::SC4_atexit, atExit);
    }
    return reinterpret_cast<GameString*>(at);
}

template <typename... Args>
void Sprintf(void* text, std::uintptr_t format, Args... args)
{
    CallCdecl<void>(addr::cRZBaseString_Sprintf, text, reinterpret_cast<const char*>(format), args...);
}

// ---- The life cycle

// 0x0054D790, the base's slot 3 (Init): 1 when already initialised (the
// occupant part's slot 5); else the core's Init 0x005101D0 and, when that
// worked and the core's byte +0x3C is clear, slot 105 without the automata
// flag 0x4000 and slot 134 without 0x2000. AL only in the game.
std::uint8_t __fastcall BaseInit(char* self, void*)
{
    static bool logged;
    FirstCall("BaseInit", logged);
    if (Virtual<std::uint8_t>(self + field::kVtable4, slot::kIsInitialised))
        return 1;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonCore_Init, self);
    if (At<std::uint8_t>(self, field::kByte3C) || !ok)
        return ok;
    if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x4000u))
        Virtual<void>(self, slot::kSlot105);
    if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x2000u))
        Virtual<void>(self, slot::kSlot134);
    return ok;
}

// 0x0055B9B0, cSC4Vehicle's slot 3: 1 when already initialised; else the
// automata flag 8 set to the byte 0x00B0A0A8, slot 158, the base's Init,
// then slot 176 (1, 0) when 0x00B43DDC's slot 9 answers yes or the byte
// 0x00B2296C is set. The base's answer, AL only in the game.
std::uint8_t __fastcall VehicleInit(char* self, void*)
{
    static bool logged;
    FirstCall("VehicleInit", logged);
    if (Virtual<std::uint8_t>(self + field::kVtable4, slot::kIsInitialised))
        return 1;
    Virtual<void>(self, slot::kSetFlag, 8u, static_cast<std::uint32_t>(Global<std::uint8_t>(addr::cSC4Vehicle_InitFlag8)));
    Virtual<void>(self, slot::kSlot158);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Init, self);
    void* const transform = Global<void*>(addr::SC4_Transform43DDC);
    if (Virtual<std::uint8_t>(transform, kTransformSlot) || Global<std::uint8_t>(addr::cSC4Vehicle_InitByte))
        Virtual<void>(self, slot::kSlot176, 1u, 0u);
    return ok;
}

// 0x00554640, on the base: the +0x204 deque's objects cleared and released,
// then the deque emptied (0x00554280; a tail jump in the game).
void __fastcall ReleaseDeque204(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseDeque204", logged);
    ReleaseElements(self + field::kDeque204, 0xC, 0x78);
    CallThis<void>(addr::SC4AutomatonDeque204_Clear, self + field::kDeque204);
}

// 0x00554280, on the +0x204 deque: emptied (0xC-byte elements, blocks of
// 0x78 bytes).
void __fastcall ClearDeque204(char* d, void*)
{
    static bool logged;
    FirstCall("ClearDeque204", logged);
    ClearDeque(d, 0xC, 0x78);
}

// 0x0055AC20, on the +0x27C deque (shared with the trains): emptied (8-byte
// elements, blocks of 0x80 bytes).
void __fastcall ClearDeque27C(char* d, void*)
{
    static bool logged;
    FirstCall("ClearDeque27C", logged);
    ClearDeque(d, 8, 0x80);
}

// 0x00555310, the base's slot 4 (Shutdown): when initialised, slot 132 with
// four zeros and 0x00554640; then the core's Shutdown 0x0050BA50 (a tail
// jump: its EAX as it is).
std::uint32_t __fastcall BaseShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("BaseShutdown", logged);
    if (Virtual<std::uint8_t>(self + field::kVtable4, slot::kIsInitialised)) {
        Virtual<void>(self, slot::kSlot132, 0u, 0u, 0u, 0u);
        CallThis<void>(addr::cSC4AutomatonBase_ReleaseDeque204, self);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Shutdown, self);
}

// 0x0055BE00, on cSC4Vehicle: each of the four models (+0x2A4..+0x2B0) taken
// off the view 0x00B43DD0 when there is one (its slot 0x74 with the model's
// slot 0), released through its virtual base and cleared.
void __fastcall ReleaseModels(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseModels", logged);
    void* const view = Global<void*>(addr::SC4_View43DD0);
    for (std::size_t i = 0; i < field::kModelCount; ++i) {
        char*& model = At<char*>(self, field::kModels + 4 * i);
        if (!model)
            continue;
        if (view)
            Virtual<void>(view, kViewRemoveSlot, Virtual<void*>(model, 0));
        ReleaseVirtual(model);
        model = nullptr;
    }
}

// 0x005602F0, cSC4Vehicle's slot 4: the base's Shutdown; +0x258 and +0x25C
// cleared and released through their virtual base; the +0x27C deque's
// objects cleared and released and the deque emptied (0x0055AC20); the
// models (0x0055BE00). The base's AL, the rest of EAX zero (0x0055BE00 ends
// with it 0).
std::uint32_t __fastcall VehicleShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("VehicleShutdown", logged);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Shutdown, self);
    for (const std::size_t at : {field::kVirtual258, field::kVirtual25C})
        if (char* const object = At<char*>(self, at)) {
            At<char*>(self, at) = nullptr;
            ReleaseVirtual(object);
        }
    ReleaseElements(self + field::kDeque27C, 8, 0x80);
    CallThis<void>(addr::SC4VehicleDeque27C_Clear, self + field::kDeque27C);
    CallThis<void>(addr::cSC4Vehicle_ReleaseModels, self);
    return ok;
}

// 0x0055CA60, cdecl (block, size): the operator delete of the classes
// derived from cSC4Vehicle: nothing for null; a 0x2B8-byte block back to the
// vehicle pool, any other to SC4::operator_delete.
void __cdecl VehicleDelete(void* block, std::uint32_t size)
{
    static bool logged;
    FirstCall("VehicleDelete", logged);
    if (!block)
        return;
    if (size == kVehicleSize)
        CallThis<void>(addr::cSC4Vehicle_PoolFree, reinterpret_cast<void*>(addr::cSC4Vehicle_Pool), block);
    else
        CallCdecl<void>(addr::SC4_operator_delete, block);
}

// ---- The +0x204 deque and the record list

// 0x00553780, on the +0x204 deque (element), ret 4: the push at the end of
// a full block: a copy of the element (its object referenced), room for a
// node at the map's back (0x008EC5B0 with 1, 0), a new 0x78-byte block, the
// element built at the finish, the finish moved to the new block, the
// copy's reference given back.
void __fastcall PushBackSlow204(char* d, void*, char* element)
{
    static bool logged;
    FirstCall("PushBackSlow204", logged);
    void* const object = At<void*>(element, 0);
    if (object)
        AddRef(object);
    const std::uint8_t byte = At<std::uint8_t>(element, 8);
    const std::uint32_t word = At<std::uint32_t>(element, 4);
    const std::uint32_t used = static_cast<std::uint32_t>(
        Difference(At<char**>(d, deque::kFinish + deque::kNode), At<char**>(d, deque::kMap)) >> 2);
    if (At<std::uint32_t>(d, deque::kMapSize) - used < 2)
        CallThis<void>(addr::SC4AutomatonDeque204_ReserveMap, d, 1u, 0u);
    char* const block = CallCdecl<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(0x78));
    At<char**>(d, deque::kFinish + deque::kNode)[1] = block;
    if (char* const current = At<char*>(d, deque::kFinish + deque::kCurrent)) {
        At<void*>(current, 0) = object;
        if (object)
            AddRef(object);
        At<std::uint32_t>(current, 4) = word;
        At<std::uint8_t>(current, 8) = byte;
    }
    char** const node = At<char**>(d, deque::kFinish + deque::kNode) + 1;
    At<char**>(d, deque::kFinish + deque::kNode) = node;
    At<char*>(d, deque::kFinish + deque::kFirst) = *node;
    At<char*>(d, deque::kFinish + deque::kLast) = *node + 0x78;
    At<char*>(d, deque::kFinish + deque::kCurrent) = At<char*>(d, deque::kFinish + deque::kFirst);
    if (object)
        Release(object);
}

// 0x00554230, on the +0x204 deque (element), ret 4: push_back of a 0xC-byte
// element (the object referenced, a word, a byte); 0x00553780 at the last
// place of a block.
void __fastcall PushBack204(char* d, void*, char* element)
{
    static bool logged;
    FirstCall("PushBack204", logged);
    char* const current = At<char*>(d, deque::kFinish + deque::kCurrent);
    if (current == At<char*>(d, deque::kFinish + deque::kLast) - 0xC) {
        CallThis<void>(addr::SC4AutomatonDeque204_PushBackSlow, d, element);
        return;
    }
    if (current) {
        void* const object = At<void*>(element, 0);
        At<void*>(current, 0) = object;
        if (object)
            AddRef(object);
        At<std::uint32_t>(current, 4) = At<std::uint32_t>(element, 4);
        At<std::uint8_t>(current, 8) = At<std::uint8_t>(element, 8);
    }
    At<char*>(d, deque::kFinish + deque::kCurrent) += 0xC;
}

// 0x00554160, on a record list (a, b, c, object), ret 0x10: a 0x40-byte node
// (SC4::stl_allocate) holding a copy of a zeroed 0x38-byte record
// (cSC4VehPedManagerBase::CopyRecord; skipped only for a node at -8), put at
// the list's back; the record's +0 a, +4 b, +8 c, +0x34 -1, its rectangles
// (cSC4VehPedManagerBase::RecordRect), +0x2C the object (referenced, the old
// one released), +0x30 zero. Answers the record (node + 8).
char* __fastcall PushRecordListBack(char** list, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c, void* object)
{
    static bool logged;
    FirstCall("PushRecordListBack", logged);
    char* const head = *list;
    alignas(4) char zero[0x38] = {};
    char* const node = CallCdecl<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(0x40));
    if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0)
        CallThis<void>(addr::cSC4VehPedManagerBase_CopyRecord, node + 8, static_cast<void*>(zero));
    char* const previous = At<char*>(head, 4);
    At<char*>(node, 4) = previous;
    At<char*>(node, 0) = head;
    At<char*>(previous, 0) = node;
    At<char*>(head, 4) = node;
    char* const record = At<char*>(*list, 4) + 8;
    At<std::uint32_t>(record, 8) = c;
    At<std::uint32_t>(record, 0x34) = 0xFFFFFFFFu;
    At<std::uint32_t>(record, 0) = a;
    At<std::uint32_t>(record, 4) = b;
    CallThis<void>(addr::cSC4VehPedManagerBase_RecordRect, list, record);
    void* const old = At<void*>(record, 0x2C);
    if (object != old) {
        if (object)
            AddRef(object);
        At<void*>(record, 0x2C) = object;
        if (old)
            Release(old);
    }
    At<std::uint8_t>(record, 0x30) = 0;
    return record;
}

// ---- The save (this = the object + 8, the serializable part)

// A stream slot with one argument (its bool answer not read, as the game's).
template <typename... Args>
void StreamCall(void* s, std::size_t at, Args... args)
{
    Virtual<std::uint8_t>(s, at, args...);
}

// 0x00551DE0, the base's +8 slot 3 (stream), ret 4: version 2 (uint16); the
// core's Write 0x0050AC00; when that worked, +0xB0 (SC4Stream::
// WriteReference), +0x1E4, the byte +0x1E8, +0x1EC (a reference), +0x1F4,
// +0x1FC, the route's size and its cells (three bytes each). The core's AL.
std::uint32_t __fastcall BaseWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("BaseWrite", logged);
    char* const self = part - field::kSerializable;
    StreamCall(s, stream::kUint16, kSaveVersion);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonCore_Write, part, s);
    if (!ok)
        return ok;
    CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(self, field::kObjectB0));
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, field::kState));
    StreamCall(s, stream::kUint8, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kByte1E8)));
    CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(self, field::kOccupant));
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, field::kBytes1F4));
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, field::k1FC));
    char* const route = self + field::kRoute;
    StreamCall(s, stream::kUint32, RouteSize(route));
    std::uint8_t* current = At<std::uint8_t*>(route, deque::kStart + deque::kCurrent);
    std::uint8_t* last = At<std::uint8_t*>(route, deque::kStart + deque::kLast);
    std::uint8_t** node = At<std::uint8_t**>(route, deque::kStart + deque::kNode);
    while (current != At<std::uint8_t*>(route, deque::kFinish + deque::kCurrent)) {
        for (int i = 0; i < 3; ++i)
            StreamCall(s, stream::kUint8, static_cast<std::uint32_t>(current[i]));
        current += 3;
        if (current == last) {
            ++node;
            current = *node;
            last = current + 0x7E;
        }
    }
    return ok;
}

// 0x00554D20, the base's +8 slot 4 (stream), ret 4: false unless the version
// (uint16) is 2; the core's Read 0x0050DFD0; when that worked, +0xB0 and
// +0x1EC cleared, released and read back (SC4Stream::ReadReference as
// 0xA9B40F05 and 0xA821EF94), +0x1E4 and +0x1FC through one local word,
// the byte +0x1E8 and +0x1F4 in place, the route emptied (SC4CellDeque::
// Clear) and its cells pushed (SC4RouteDeque::PushBackSlow at a block's
// last place). The core's AL.
std::uint32_t __fastcall BaseRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("BaseRead", logged);
    char* const self = part - field::kSerializable;
    std::uint16_t version = 0;
    StreamCall(s, stream::kUint16, &version);
    if (version != kSaveVersion)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonCore_Read, part, s);
    if (!ok)
        return ok;
    ClearReference(self, field::kObjectB0);
    CallCdecl<void>(addr::SC4Stream_ReadReference, s, kObjectB0Iid, &At<void*>(self, field::kObjectB0), 0u);
    std::uint32_t word = 0;
    StreamCall(s, stream::kUint32, &word);
    At<std::uint32_t>(self, field::kState) = word;
    StreamCall(s, stream::kUint8, &At<std::uint8_t>(self, field::kByte1E8));
    ClearReference(self, field::kOccupant);
    CallCdecl<void>(addr::SC4Stream_ReadReference, s, kOccupantIid, &At<void*>(self, field::kOccupant), 0u);
    StreamCall(s, stream::kUint32, &At<std::uint32_t>(self, field::kBytes1F4));
    StreamCall(s, stream::kUint32, &word);
    At<std::uint32_t>(self, field::k1FC) = word;
    char* const route = self + field::kRoute;
    CallThis<void>(addr::SC4CellDeque_Clear, route);
    std::uint32_t count = 0;
    StreamCall(s, stream::kUint32, &count);
    std::uint8_t cell[3] = {};
    for (std::uint32_t i = 0; i < count; ++i) {
        for (std::uint8_t& byte : cell)
            StreamCall(s, stream::kUint8, &byte);
        char*& current = At<char*>(route, deque::kFinish + deque::kCurrent);
        if (current != At<char*>(route, deque::kFinish + deque::kLast) - 3) {
            if (current)
                std::memcpy(current, cell, 3);
            current += 3;
        } else {
            CallThis<void>(addr::SC4RouteDeque_PushBackSlow, route, static_cast<void*>(cell));
        }
    }
    return ok;
}

// 0x0055F640, cSC4Vehicle's +8 slot 3 (stream), ret 4: version 2; the base's
// Write; when that worked, the +0x27C deque's size and per element its word
// (uint32) and the two words its object's slot 0xEC answers (sint32 each;
// the object is not tested for null), a zero word, the point +0x264, the
// core's default point 0x00B09A04 (not +0x270) and the float +0x260. The
// base's AL.
std::uint32_t __fastcall VehicleWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("VehicleWrite", logged);
    char* const self = part - field::kSerializable;
    StreamCall(s, stream::kUint16, kSaveVersion);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Write, part, s);
    if (!ok)
        return ok;
    char* const d = self + field::kDeque27C;
    StreamCall(s, stream::kUint32, QueueSize(d));
    char* current = At<char*>(d, deque::kStart + deque::kCurrent);
    char* last = At<char*>(d, deque::kStart + deque::kLast);
    char** node = At<char**>(d, deque::kStart + deque::kNode);
    while (current != At<char*>(d, deque::kFinish + deque::kCurrent)) {
        StreamCall(s, stream::kUint32, At<std::uint32_t>(current, 4));
        std::int32_t first = 0, second = 0;
        Virtual<void>(At<void*>(current, 0), kQueueKeySlot, &first, &second);
        StreamCall(s, stream::kSint32, first);
        StreamCall(s, stream::kSint32, second);
        current += 8;
        if (current == last) {
            ++node;
            current = *node;
            last = current + 0x80;
        }
    }
    StreamCall(s, stream::kUint32, 0u);
    CallCdecl<void>(addr::SC4Stream_WriteVector3, s, self + field::kPoint264);
    CallCdecl<void>(addr::SC4Stream_WriteVector3, s, reinterpret_cast<const void*>(addr::cSC4AutomatonCore_DefaultPoint));
    StreamCall(s, stream::kFloat32, At<float>(self, field::kFloat260));
    return ok;
}

// 0x0055BCF0, cSC4Vehicle's +8 slot 4 (stream), ret 4: false unless the
// version is 2; the base's Read; when that worked, the deque's elements read
// (a word and two sint32 each) and dropped: the object the loop would
// release is a local it sets to null, so the +0x27C deque is not filled;
// then a word read and dropped, the point +0x264, a second point read into a
// local (three floats 0x00B22978 first) and dropped, the float +0x260. The
// base's AL.
std::uint32_t __fastcall VehicleRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("VehicleRead", logged);
    char* const self = part - field::kSerializable;
    std::uint16_t version = 0;
    StreamCall(s, stream::kUint16, &version);
    if (version != kSaveVersion)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_Read, part, s);
    if (!ok)
        return ok;
    std::uint32_t count = 0;
    StreamCall(s, stream::kUint32, &count);
    std::uint32_t word = 0;
    std::int32_t first = 0, second = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        void* object = nullptr;
        StreamCall(s, stream::kUint32, &word);
        StreamCall(s, stream::kSint32, &first);
        StreamCall(s, stream::kSint32, &second);
        if (object)
            Release(object);
    }
    const std::uint32_t start = Global<std::uint32_t>(addr::cSC4Vehicle_ReadPointInit);
    std::uint32_t point[3] = {start, start, start};
    std::uint32_t zero = 0;
    StreamCall(s, stream::kUint32, &zero);
    CallCdecl<void>(addr::SC4Stream_ReadVector3, s, self + field::kPoint264);
    CallCdecl<void>(addr::SC4Stream_ReadVector3, s, static_cast<void*>(point));
    StreamCall(s, stream::kFloat32, &At<float>(self, field::kFloat260));
    return ok;
}

// ---- The setters

// 0x00504FC0, slot 154 (byte), ret 4: +0x1E8.
void __fastcall SetByte1E8(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("SetByte1E8", logged);
    At<std::uint8_t>(self, field::kByte1E8) = static_cast<std::uint8_t>(value);
}

void CopyPoint(char* self, std::size_t at, const std::uint32_t* point)
{
    for (int i = 0; i < 3; ++i)
        At<std::uint32_t>(self, at + 4 * i) = point[i];
}

// 0x00505110, cSC4Vehicle's slot 178 (point), ret 4: +0x264 (three words).
void __fastcall SetPoint264(char* self, void*, const std::uint32_t* point)
{
    static bool logged;
    FirstCall("SetPoint264", logged);
    CopyPoint(self, field::kPoint264, point);
}

// 0x00505140, cSC4Vehicle's slot 180 (point), ret 4: +0x270.
void __fastcall SetPoint270(char* self, void*, const std::uint32_t* point)
{
    static bool logged;
    FirstCall("SetPoint270", logged);
    CopyPoint(self, field::kPoint270, point);
}

// 0x0053CC00, the base's slot 158 (also three other classes'): +0x200 zero.
void __fastcall ClearFloat200(char* self, void*)
{
    static bool logged;
    FirstCall("ClearFloat200", logged);
    At<std::uint32_t>(self, field::kFloat200) = 0;
}

// 0x0055B690, cSC4Vehicle's slot 158: +0x200 a float from the automata's
// generator between the floats 0x00B21CF0 and 0x00B21CF4 (cRZRandom::Range,
// as doubles).
void __fastcall DrawFloat200(char* self, void*)
{
    static bool logged;
    FirstCall("DrawFloat200", logged);
    const double low = Global<float>(addr::cSC4Vehicle_Float200Low);
    const double high = Global<float>(addr::cSC4Vehicle_Float200High);
    At<float>(self, field::kFloat200) = static_cast<float>(
        CallThis<double>(addr::cRZRandom_Range, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random), low, high));
}

// 0x0055B750, cSC4Vehicle's slot 184: the automata flags 0x0201180A set, the
// byte +0x1E8 drawn below 50 (SC4Random::Below on the automata's generator).
// AL 0.
std::uint32_t __fastcall DrawByte1E8(char* self, void*)
{
    static bool logged;
    FirstCall("DrawByte1E8", logged);
    Virtual<void>(self, slot::kSetFlag, 0x0201180Au, 1u);
    At<std::uint8_t>(self, field::kByte1E8) =
        CallThis<std::uint8_t>(addr::SC4Random_Below, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random), 0x32u);
    return 0;
}

// The rate of slots 159 and 160: the factor over slot 125's answer when that
// is above 0 (FCOM: below, equal or unordered take 0.1).
float SpeedRate(char* self, std::uintptr_t factor)
{
    const double value = Virtual<double>(self, slot::kSlot125);
    if (!(value > static_cast<double>(Global<float>(addr::SC4_FloatZero))))
        return 0.1f; // 0x3DCCCCCD
    return static_cast<float>(static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)) / value *
                              static_cast<double>(Global<float>(factor)));
}

// 0x0054DA00, slot 159: SetSpeed (slot 36) to 0 at the rate 1.5
// (0x00A880D4) over slot 125's answer.
void __fastcall Stop(char* self, void*)
{
    static bool logged;
    FirstCall("Stop", logged);
    const float rate = SpeedRate(self, addr::cSC4Demolition_TerrainBoxMargin);
    Virtual<void>(self, slot::kSetSpeed, 0.0f, rate);
}

// 0x0054DA50, slot 160: SetSpeed to the default speed (slot 35) at the rate
// 0.3 (0x00A97D74) over slot 125's answer.
void __fastcall Go(char* self, void*)
{
    static bool logged;
    FirstCall("Go", logged);
    const float rate = SpeedRate(self, addr::cSC4CitySituationManager_IntensityFactor);
    const float target = static_cast<float>(Virtual<double>(self, slot::kGetDefaultSpeed));
    Virtual<void>(self, slot::kSetSpeed, target, rate);
}

// 0x0054EB20, slot 149 (value), ret 4: nothing when the byte is the flag 8's
// (slot 150, its AL); else the flag 8 set with the whole word (slot 30), and
// for a set byte +0xB0 to slot 148's object (referenced, the old one
// released); then slot 124. The game leaves the last call's EAX.
std::uint32_t __fastcall SetFlag8(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("SetFlag8", logged);
    const std::uint32_t current = Virtual<std::uint32_t>(self, slot::kFlag8);
    if (static_cast<std::uint8_t>(value) == static_cast<std::uint8_t>(current))
        return current;
    Virtual<void>(self, slot::kSetFlag, 8u, value);
    if (static_cast<std::uint8_t>(value)) {
        void* const found = Virtual<void*>(self, slot::kSlot148);
        void* const old = At<void*>(self, field::kObjectB0);
        if (found != old) {
            if (found)
                AddRef(found);
            At<void*>(self, field::kObjectB0) = found;
            if (old)
                Release(old);
        }
    }
    return Virtual<std::uint32_t>(self, slot::kForget);
}

// 0x0054D820, the base's slot 135: Remove (slot 13) with false; its EAX.
std::uint32_t __fastcall RemoveNow(char* self, void*)
{
    static bool logged;
    FirstCall("RemoveNow", logged);
    return Virtual<std::uint32_t>(self, slot::kRemove, 0u);
}

// 0x0054D830, slot 172 (a, b, c, d), ret 0x10: slot 173 (a, b), Remove with
// false; AL 0. The third and fourth words are not read.
std::uint32_t __fastcall Slot172(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t, std::uint32_t)
{
    static bool logged;
    FirstCall("Slot172", logged);
    Virtual<void>(self, slot::kSlot173, a, b);
    Virtual<void>(self, slot::kRemove, 0u);
    return 0;
}

// 0x0055BE70, cSC4Vehicle's slot 124: the city's vehicle manager (city slot
// 43) forgets it (its slot 69); that EAX.
std::uint32_t __fastcall Forget(char* self, void*)
{
    static bool logged;
    FirstCall("Forget", logged);
    void* const city = Global<void*>(addr::SC4_City);
    void* const manager = Virtual<void*>(city, kCityVehicleManager);
    return Virtual<std::uint32_t>(manager, kManagerForget, static_cast<void*>(self));
}

// ---- The debug texts (slot 93)

// 0x00553CF0, the base's slot 93: the static string 0x00B22924 formatted
// "layer: %d  lifespan: %d  %s %s %s" with +0x1E4, slot 153, "edge: %d-%d"
// (+0x1F5, +0x1F4), "route: %d, %d" (the route's first cell's two first
// bytes; empty without cells) and the core's text (0x0050F0A0, its
// ToChar). Answers the string.
void* __fastcall BaseDebugText(char* self, void*)
{
    static bool logged;
    FirstCall("BaseDebugText", logged);
    GameString* const text = StaticString(addr::cSC4AutomatonBase_DebugTextString, addr::cSC4AutomatonBase_DebugTextGuard,
                                          addr::cSC4AutomatonBase_DebugTextAtExit);
    GameString route;
    MakeString(route);
    const std::uint8_t* const cell = At<std::uint8_t*>(self, field::kRoute + deque::kStart + deque::kCurrent);
    if (At<std::uint8_t*>(self, field::kRoute + deque::kFinish + deque::kCurrent) != cell)
        Sprintf(&route, addr::cSC4AutomatonBase_RouteFormat, static_cast<std::uint32_t>(cell[0]),
                static_cast<std::uint32_t>(cell[1]));
    GameString edge;
    MakeString(edge);
    Sprintf(&edge, addr::cSC4AutomatonBase_EdgeFormat, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kByte1F5)),
            static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kBytes1F4)));
    void* const core = CallThis<void*>(addr::cSC4AutomatonCore_DebugText, self);
    const std::uint32_t state = At<std::uint32_t>(self, field::kState);
    const char* const coreText = Virtual<const char*>(core, kStringToChar);
    const char* const routeText = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&route));
    const char* const edgeText = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&edge));
    const std::uint32_t lifespan = Virtual<std::uint8_t>(self, slot::kGetByte1E8);
    Sprintf(text, addr::cSC4AutomatonBase_DebugFormat, state, lifespan, edgeText, routeText, coreText);
    FreeString(edge);
    FreeString(route);
    return text;
}

// 0x00562AC0, cSC4Vehicle's slot 93: the static string 0x00B22988 assigned
// the base's text (SC4String::AssignRange, unless it is the same string) and
// appended " model:0x%8x 0x%8x 0x%8x" with the first model key's three
// words (+0xC0; zeros without one). Answers the string.
void* __fastcall VehicleDebugText(char* self, void*)
{
    static bool logged;
    FirstCall("VehicleDebugText", logged);
    GameString* const text = StaticString(addr::cSC4Vehicle_DebugTextString, addr::cSC4Vehicle_DebugTextGuard,
                                          addr::cSC4Vehicle_DebugTextAtExit);
    std::uint32_t key[3] = {};
    const std::uint32_t* const keys = At<const std::uint32_t*>(self, field::kModelKeys);
    if (keys != At<const std::uint32_t*>(self, field::kModelKeys + 4))
        for (int i = 0; i < 3; ++i)
            key[i] = keys[i];
    char* const base = CallThis<char*>(addr::cSC4AutomatonBase_DebugText, self);
    char* const inner = base ? base + 4 : nullptr;
    char* const own = reinterpret_cast<char*>(text) + 4;
    char allocator = 0; // the game passes a byte of its stack
    if (inner != own)
        CallThis<void>(addr::SC4String_AssignRange, own, At<char*>(inner, 0), At<char*>(inner, 4),
                       static_cast<void*>(&allocator));
    GameString model;
    MakeString(model);
    Sprintf(&model, addr::cSC4Vehicle_ModelFormat, key[0], key[1], key[2]);
    CallThis<void>(addr::SC4String_AppendRange, own, model.begin, model.end, static_cast<void*>(&allocator));
    FreeString(model);
    return text;
}

// ====================================================================================
// Part vb3: the movement on frames

void* City() { return Global<void*>(addr::SC4_City); }

float Zero() { return Global<float>(addr::SC4_FloatZero); }
float One() { return Global<float>(addr::cSC4AutomatonBase_One); }

// FCOS and FSIN of a double, as the game's x87 computes them.
double X87Cos(double x)
{
    double result;
    __asm {
        fld x
        fcos
        fstp result
    }
    return result;
}

double X87Sin(double x)
{
    double result;
    __asm {
        fld x
        fsin
        fstp result
    }
    return result;
}

void CopyThree(float* to, const float* from) { std::memcpy(to, from, 12); }

// The signed count of a vector of 12-byte points (begin, end), as the game's
// IMUL by 0x2AAAAAAB divides.
std::int32_t PointCount(char* const* path)
{
    return static_cast<std::int32_t>(path[1] - path[0]) / 12;
}

// 0x00551F70, on the route deque: pop_front of a 3-byte cell; at the
// block's last cell the block is freed (SC4::Free) and the start moves to
// the next node's block (0x7E bytes). Answers EAX as the game leaves it.
char* __fastcall RoutePopFront(char* deque, void*)
{
    static bool logged;
    FirstCall("RoutePopFront", logged);
    char*& current = At<char*>(deque, deque::kStart + deque::kCurrent);
    if (current != At<char*>(deque, deque::kStart + deque::kLast) - 3) {
        current += 3;
        return current;
    }
    char* const first = At<char*>(deque, deque::kStart + deque::kFirst);
    if (first)
        CallCdecl<void>(addr::SC4_Free, first);
    char** const node = ++At<char**>(deque, deque::kStart + deque::kNode);
    At<char*>(deque, deque::kStart + deque::kFirst) = *node;
    At<char*>(deque, deque::kStart + deque::kLast) = *node + 0x7E;
    current = *node;
    return *node + 0x7E;
}

// 0x0054E5B0, slot 10 (point), ret 4: the automata flag 4 cleared, the
// destination +0x4C set; the heading +0x58 the unit vector to it in x and z
// (the y difference is the destination's y less itself), then slot 16
// (null, heading). At a length not above 0 the flag 4 set and slot 102
// (4, 0) instead. Answers the last call's EAX.
std::uint32_t __fastcall SetDestination(char* self, void*, const float* point)
{
    static bool logged;
    FirstCall("SetDestination", logged);
    Virtual<std::uint32_t>(self, slot::kSetFlag, 4u, 0u);
    float* const destination = &At<float>(self, field::kDestination);
    CopyThree(destination, point);
    const float* const position = &At<float>(self, field::kPosition);
    const float dx = static_cast<float>(static_cast<double>(destination[0]) - position[0]);
    const double y = destination[1];
    const float dy = static_cast<float>(y - y);
    const double dz = static_cast<double>(destination[2]) - position[2];
    const double length = std::sqrt((dz * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
    if (!(length > Zero())) {
        Virtual<std::uint32_t>(self, slot::kSetFlag, 4u, 1u);
        return Virtual<std::uint32_t>(self, slot::kSlot102, 4u, 0u);
    }
    const double inverse = One() / length;
    float* const heading = &At<float>(self, field::kHeading);
    heading[2] = static_cast<float>(dz * inverse);
    heading[0] = static_cast<float>(dx * inverse);
    heading[1] = static_cast<float>(dy * inverse);
    return Virtual<std::uint32_t>(self, slot::kSlot16, 0u, heading);
}

// 0x0054E790, slot 170 (network, float weights[4][4]), ret 8: false without
// a network; +0xA0 = 100 when slot 122 (+0xA4) answers, else false when +0x98
// is zero or the automata flag 0x4000 is on. Then +0xA4 normalised (not
// tested for zero), the best side (slot 171 with 1, its out the network
// argument's stack slot) and, unless none, for each of the four rows the
// network answers for (its slot 0xD4 with 0, the row and that slot's word),
// a share of the opposite side's weight (at least 100, times |+0xA0| x 0.01)
// moved to the side, the rest not below 0. AL only.
std::uint8_t __fastcall WeighSides(char* self, void*, void* network, float* weights)
{
    static bool logged;
    FirstCall("WeighSides", logged);
    if (!network)
        return 0;
    float* const v = &At<float>(self, field::kVectorA4);
    if (Virtual<std::uint8_t>(self, slot::kSlot122, v)) {
        At<std::uint32_t>(self, field::kFloatA0) = 0x42C80000u; // 100.0
    } else {
        if (!At<std::uint32_t>(self, field::k98))
            return 0;
        if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x4000u))
            return 0;
    }
    Virtual<void*>(self, slot::kManager);
    const double x = v[0], y = v[1], z = v[2];
    const double inverse = One() / std::sqrt((x * x + y * y) + z * z);
    v[0] = static_cast<float>(inverse * v[0]);
    v[1] = static_cast<float>(inverse * v[1]);
    v[2] = static_cast<float>(inverse * v[2]);
    // The game passes the network argument's own stack slot as the out byte:
    // its upper three bytes stay the pointer's.
    std::uint32_t sideSlot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(network));
    Virtual<std::uint32_t>(self, slot::kBestDirection, network, static_cast<const float*>(v),
                           reinterpret_cast<std::uint8_t*>(&sideSlot), 1u);
    const std::uint32_t side = sideSlot & 0xFFu;
    const std::uint32_t opposite = (side + 2u) & 3u;
    const float share = static_cast<float>(std::fabs(static_cast<double>(At<float>(self, field::kFloatA0))) *
                                           Global<float>(addr::cSC4AutomatonBase_SideShare));
    if (side == 0xFFu)
        return 1;
    const float least = Global<float>(addr::cSC4AutomatonBase_SideWeightLeast);
    for (std::uint32_t row = 0; row < 4; ++row) {
        // The row is a byte with three bytes of the game's stack above it.
        if (!Virtual<std::uint8_t>(network, kNeighbourSlot, 0u, row, sideSlot))
            continue;
        float* const from = &weights[row * 4 + opposite];
        double moved = *from;
        if (!(moved > least))
            moved = least;
        moved *= share;
        float* const to = &weights[row * 4 + side];
        *to = static_cast<float>(moved + *to);
        double rest = *from - moved;
        if (!(rest > Zero()))
            rest = Zero();
        *from = static_cast<float>(rest);
    }
    return 1;
}

// 0x0054E6F0, on the automaton (a, b, side, d), ret 0x10: a 4 x 4 table of
// side weights from the vehicle manager (slot 174's: its slot 72 with a, b,
// +0x1E4 and its slot 42's weight for that state), slot 170 (a, table), then
// the traffic network map's slot 6 with the table's row `side` (a byte) and
// d. Answers that call's EAX.
std::uint32_t __fastcall ChooseSide(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t side,
                                    std::uint32_t d)
{
    static bool logged;
    FirstCall("ChooseSide", logged);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    void* const vehicles = Virtual<void*>(self, slot::kManager);
    const std::uint32_t state = At<std::uint32_t>(self, field::kState);
    const float weight = static_cast<float>(Virtual<double>(vehicles, manager::kStateWeight, state));
    float table[16] = {}; // stack garbage in the game; the manager fills it
    Virtual<std::uint32_t>(vehicles, manager::kWeights, a, b, At<std::uint32_t>(self, field::kState), weight,
                           static_cast<float*>(table));
    Virtual<std::uint8_t>(self, slot::kWeighSides, a, static_cast<float*>(table));
    return Virtual<std::uint32_t>(map, map::kChoose, static_cast<const float*>(&table[(side & 0xFFu) * 4]), d);
}

// 0x0054E920, slot 131 (occupant, key, exact), ret 0xC: the occupant's
// network part (slot 49; false without one); its path for the key (slot 3
// with 0) and, with exact and no such path, one found from the position
// (slot 5 with the key's kind (bits 24..27), its byte 1, the key, the
// position, an out word and 0); false without a path of two points or more.
// Then slot 132 (occupant, the out word, the path, exact's word) and the
// traffic network map's slot 19 word for the occupant, +0x1E4, the kind, the
// key's byte 1 and the key set to 0. AL only.
std::uint8_t __fastcall PathTo(char* self, void*, void* occupant, std::uint32_t key, std::uint32_t exact)
{
    static bool logged;
    FirstCall("PathTo", logged);
    void* const network = Virtual<void*>(occupant, kNetworkPartSlot);
    std::uint32_t found = key;
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    const std::uint32_t kind = (key >> 24) & 0xFu;
    const std::uint32_t entry = (key >> 8) & 0xFFu; // a byte with three bytes of the game's stack above it
    if (!network)
        return 0;
    char** path = nullptr;
    if (static_cast<std::uint8_t>(exact)) {
        path = Virtual<char**>(network, part::kPath, key, 0u);
        if (!path) {
            const float* const position = Virtual<const float*>(self, slot::kPosition);
            path = Virtual<char**>(network, part::kFindPath, kind, entry, key, position, &found, 0u);
        }
    } else {
        path = Virtual<char**>(network, part::kPath, found, 0u);
    }
    if (!path)
        return 0;
    if (static_cast<std::uint32_t>(PointCount(path)) < 2)
        return 0;
    Virtual<std::uint32_t>(self, slot::kSlot132, occupant, found, path, exact);
    std::uint32_t* const word = Virtual<std::uint32_t*>(map, map::kLaneWord, occupant,
                                                        At<std::uint32_t>(self, field::kState), kind, entry, key);
    *word = 0;
    return 1;
}

// 0x00550760, slot 132 (occupant, groups, path, keep), ret 0x10: the
// occupant +0x1EC set (referenced); with an occupant its network part (slot
// 49, referenced) and, without a path given, the part's for the groups
// (slot 3 with 0). With a path and a part: +0x1F4 the groups, +0x1F0 the
// part, +0x1F8 the path, +0x1FC 0; with keep the automata flag 4 cleared,
// else the path's first two points offset sideways (slot 157), their
// heights from the occupant (slot 56) unless the part is flat (slot 16),
// slot 16 (first, the unit vector between) and SetDestination (second).
// Without: +0x1F8, +0x1F0 and +0x1FC cleared. When the occupant changed:
// into a tunnel (0x8A4BD52B by QueryInterface, after the occupant's slot 8
// with 0x80000) slot 33 (0, 0.8) and the sound object (0x00602640) slot 5
// with 0xE when slot 57 (0x10) answers; out of one slot 33 (1, 0.1) and its
// slot 6. Then the traffic network map forgets the automaton on the old
// occupant (slot 31 with the old groups) and, with an occupant, slot 160,
// the map's slot 30 and, with the flag 8, +0xB0 = slot 148 (SC4RefPtr).
void __fastcall EnterOccupant(char* self, void*, void* occupant, std::uint32_t groups, char** path,
                              std::uint32_t keep)
{
    static bool logged;
    FirstCall("EnterOccupant", logged);
    void* network = nullptr;
    void* const old = At<void*>(self, field::kOccupant);
    if (old)
        AddRef(old);
    const std::uint32_t oldGroups = Virtual<std::uint32_t>(self, slot::kGroups);
    void* const was = At<void*>(self, field::kOccupant);
    if (occupant != was) {
        if (occupant)
            AddRef(occupant);
        At<void*>(self, field::kOccupant) = occupant;
        if (was)
            Release(was);
    }
    if (occupant) {
        network = Virtual<void*>(At<void*>(self, field::kOccupant), kNetworkPartSlot);
        if (network) {
            AddRef(network);
            if (!path)
                path = Virtual<char**>(network, part::kPath, groups, 0u);
        }
    }
    if (path && network) {
        At<std::uint32_t>(self, field::kGroups1F4) = groups;
        void* const before = At<void*>(self, field::kNetwork);
        if (network != before) {
            AddRef(network);
            At<void*>(self, field::kNetwork) = network;
            if (before)
                Release(before);
        }
        At<char**>(self, field::kPath) = path;
        const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
        float first[3] = {g, g, g}, second[3] = {g, g, g};
        At<std::uint32_t>(self, field::kPathIndex) = 0;
        if (static_cast<std::uint8_t>(keep)) {
            Virtual<std::uint32_t>(self, slot::kSetFlag, 4u, 0u);
        } else {
            CopyThree(first, reinterpret_cast<const float*>(At<char**>(self, field::kPath)[0]));
            Virtual<std::uint32_t>(self, slot::kOffsetSideways, static_cast<float*>(first));
            if (!Virtual<std::uint8_t>(network, part::kFlat))
                first[1] = static_cast<float>(
                    Virtual<double>(At<void*>(self, field::kOccupant), kOccupantHeight, first[0], first[2]));
            CopyThree(second, reinterpret_cast<const float*>(At<char**>(self, field::kPath)[0]) + 3);
            Virtual<std::uint32_t>(self, slot::kOffsetSideways, static_cast<float*>(second));
            if (!Virtual<std::uint8_t>(network, part::kFlat))
                second[1] = static_cast<float>(
                    Virtual<double>(At<void*>(self, field::kOccupant), kOccupantHeight, second[0], second[2]));
            const double dx = static_cast<double>(second[0]) - first[0];
            const double dy = static_cast<double>(second[1]) - first[1];
            const double dz = static_cast<double>(second[2]) - first[2];
            const float dzf = static_cast<float>(dz);
            const float inverse = static_cast<float>(One() / std::sqrt((dz * dzf + dy * dy) + dx * dx));
            float heading[3] = {static_cast<float>(dx * inverse), static_cast<float>(dy * inverse),
                                static_cast<float>(static_cast<double>(dzf) * inverse)};
            Virtual<std::uint32_t>(self, slot::kSlot16, static_cast<float*>(first), static_cast<float*>(heading));
            Virtual<std::uint32_t>(self, slot::kSetDestination, static_cast<float*>(second));
        }
    } else {
        At<char**>(self, field::kPath) = nullptr;
        void* const before = At<void*>(self, field::kNetwork);
        if (before) {
            At<void*>(self, field::kNetwork) = nullptr;
            Release(before);
        }
        At<std::uint32_t>(self, field::kPathIndex) = 0;
    }
    void* const now = At<void*>(self, field::kOccupant);
    if (now != old) {
        void* entered = nullptr;
        void* left = nullptr;
        if (now && Virtual<std::uint8_t>(now, kOccupantFlag, 0x80000u))
            Virtual<std::uint32_t>(At<void*>(self, field::kOccupant), slot::kQueryInterface, kTunnelIid, &entered);
        if (old && Virtual<std::uint8_t>(old, kOccupantFlag, 0x80000u))
            Virtual<std::uint32_t>(old, slot::kQueryInterface, kTunnelIid, &left);
        if (entered || left) {
            const bool in = entered && !left;
            if (in)
                Virtual<std::uint32_t>(self, slot::kFade, 0.0f, 0.8f);
            else
                Virtual<std::uint32_t>(self, slot::kFade, 1.0f, 0.1f);
            if (Virtual<std::uint32_t>(self, slot::kSlot57, 0x10u)) {
                void* const sounds = CallCdecl<void*>(addr::SC4UDrive_GetService602640);
                Virtual<std::uint32_t>(sounds, in ? 0x14u : 0x18u, 0xEu);
            }
            if (entered)
                Release(entered);
            if (left)
                Release(left);
        }
    }
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    if (map) {
        if (old)
            Virtual<std::uint32_t>(map, map::kRemoveAutomaton, self, old, oldGroups);
        if (At<void*>(self, field::kOccupant)) {
            Virtual<std::uint32_t>(self, slot::kGo);
            Virtual<std::uint32_t>(map, map::kAddAutomaton, self, At<void*>(self, field::kOccupant));
            if (Virtual<std::uint8_t>(self, slot::kFlag8)) {
                void* const front = Virtual<void*>(self, slot::kSlot148);
                CallThis<void>(addr::SC4RefPtr_Assign, self + field::kObjectB0, front);
            }
        }
    }
    if (old)
        Release(old);
    if (network)
        Release(network);
}

// 0x005504E0, slot 133 (step, from, out, index, path, leftover), ret 0x18:
// out = from, then along the path (+0x1F8 when null; false without one)
// from point index (+0x1FC when null; below 0 starts at 0, at the last point
// or beyond leftover = step and false) by step: each next point offset
// sideways (slot 157), the segment's unit vector (when its length is above
// 1e-5); within the segment out moves by step along it and the answer is
// true; else on to the next point with the step less the segment. Past the
// last point, with the segment and the step above 0, leftover the rest and
// out moved by the whole step along the last segment; false. AL only.
std::uint8_t __fastcall AdvanceOnPath(char* self, void*, float step, const float* from, float* out,
                                      std::int32_t* index, char** path, float* leftover)
{
    static bool logged;
    FirstCall("AdvanceOnPath", logged);
    const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
    float point[3] = {g, g, g};
    if (leftover)
        At<std::uint32_t>(leftover, 0) = 0;
    if (!path) {
        path = At<char**>(self, field::kPath);
        if (!path)
            return 0;
    }
    if (!index)
        index = &At<std::int32_t>(self, field::kPathIndex);
    const std::int32_t count = PointCount(path);
    CopyThree(out, from);
    if (*index < 0) {
        *index = 0;
    } else if (!(static_cast<std::uint32_t>(*index) < static_cast<std::uint32_t>(count - 1))) {
        if (leftover)
            *leftover = step;
        return 0;
    }
    const std::uint32_t limit = static_cast<std::uint32_t>(count - 1);
    const float epsilon = Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon);
    float heading[3];
    auto move = [&] {
        const double mx = heading[0] * static_cast<double>(step);
        const double my = heading[1] * static_cast<double>(step);
        const float mz = static_cast<float>(heading[2] * static_cast<double>(step));
        out[0] = static_cast<float>(mx + out[0]);
        out[1] = static_cast<float>(my + out[1]);
        out[2] = static_cast<float>(static_cast<double>(mz) + out[2]);
    };
    while (static_cast<std::uint32_t>(*index) < limit) {
        CopyThree(point, reinterpret_cast<const float*>(path[0]) + 3 * (*index + 1));
        Virtual<std::uint32_t>(self, slot::kOffsetSideways, static_cast<float*>(point));
        const double dx = static_cast<double>(point[0]) - out[0];
        const float dy = static_cast<float>(static_cast<double>(point[1]) - out[1]);
        const double dz = static_cast<double>(point[2]) - out[2];
        heading[0] = static_cast<float>(dx);
        heading[1] = dy;
        heading[2] = static_cast<float>(dz);
        const double length = std::sqrt((dz * dz + static_cast<double>(dy) * dy) + dx * dx);
        const float segment = static_cast<float>(length);
        if (segment > epsilon) {
            const double inverse = One() / length;
            heading[0] = static_cast<float>(dx * inverse);
            heading[1] = static_cast<float>(dy * inverse);
            heading[2] = static_cast<float>(dz * inverse);
        }
        if (segment >= step) {
            move();
            return 1;
        }
        ++*index;
        if (!(static_cast<std::uint32_t>(*index) < limit)) {
            if (segment > Zero() && step > Zero()) {
                if (leftover)
                    *leftover = static_cast<float>(static_cast<double>(step) - segment);
                move();
            }
            return 0;
        }
        step = static_cast<float>(static_cast<double>(step) - segment);
        CopyThree(out, point);
    }
    return 0;
}

// 0x005510D0, slot 157 (point), ret 4: the point moved sideways by +0x200
// (plus slot 56's object's +0x24 object's +0x4C when +0xB4..+0xB8 has
// elements; nothing below 1e-4): the path's (+0x1F8, not null-tested) first
// to last direction, its angle (0x007F82F0, an FPATAN of the negated x and
// z), its FSIN and FCOS turned into a sideways vector whose sign is the
// direction's dot product with (1/sqrt 2, 0, 1/sqrt 2) below 0 or not.
void __fastcall OffsetSideways(char* self, void*, float* point)
{
    static bool logged;
    FirstCall("OffsetSideways", logged);
    float offset = At<float>(self, field::kFloat200);
    if (At<std::uint32_t>(self, field::kVectorB4) != At<std::uint32_t>(self, field::kVectorB4 + 4)) {
        char* const object = Virtual<char*>(self, slot::kSlot56);
        if (object) {
            char* const inner = At<char*>(object, 0x24);
            if (inner)
                offset = static_cast<float>(static_cast<double>(offset) + At<float>(inner, 0x4C));
        }
    }
    if (std::fabs(static_cast<double>(offset)) < Global<float>(addr::cSC4AutomatonBase_PlaceEpsilon))
        return;
    const float zero = Zero();
    const double q = One() / std::sqrt(Global<double>(addr::SC4_DoubleTwo));
    const double a = static_cast<double>(static_cast<float>(q)) * zero;
    char* const* const path = At<char**>(self, field::kPath);
    const float* const first = reinterpret_cast<const float*>(path[0]);
    const float* const last = reinterpret_cast<const float*>(path[1]) - 3;
    const double dx = static_cast<double>(last[0]) - first[0];
    const double dy = static_cast<double>(last[1]) - first[1];
    const double dz = static_cast<double>(last[2]) - first[2];
    const float dzf = static_cast<float>(dz);
    const float inverse = static_cast<float>(One() / std::sqrt((dz * dzf + dy * dy) + dx * dx));
    const float nx = static_cast<float>(dx * inverse);
    const double ny = dy * inverse;
    const double nzu = static_cast<double>(dzf) * inverse;
    const float nz = static_cast<float>(nzu);
    const double dot = (nzu * q + ny * a) + static_cast<double>(nx) * q;
    const float sign = dot < zero ? 1.0f : -1.0f;
    const double angle = CallCdecl<double>(addr::SC4_NegatedAtan2, nx, nz);
    const float c = static_cast<float>(X87Cos(angle));
    const double s = X87Sin(angle);
    const double zs = zero * s;
    const float zsf = static_cast<float>(zs);
    const float sf = static_cast<float>(s);
    const double zc = static_cast<double>(c) * zero;
    const float zcf = static_cast<float>(zc);
    const float across[3] = {static_cast<float>(static_cast<double>(c) - zsf), static_cast<float>(zc - zsf),
                             static_cast<float>(zc - sf)};
    const float along[3] = {static_cast<float>(static_cast<double>(sf) + zcf),
                            static_cast<float>(static_cast<double>(zsf) + zcf), static_cast<float>(zs + c)};
    const double rx = static_cast<double>(across[0]) * sign + static_cast<double>(along[0]) * zero;
    const double ry = static_cast<double>(across[1]) * sign + static_cast<double>(along[1]) * zero;
    const float rz = static_cast<float>(static_cast<double>(across[2]) * sign + static_cast<double>(along[2]) * zero);
    const float ox = static_cast<float>(static_cast<double>(static_cast<float>(rx)) * offset);
    const float oy = static_cast<float>(ry * offset);
    point[0] = static_cast<float>(static_cast<double>(ox) + point[0]);
    point[1] = static_cast<float>(static_cast<double>(oy) + point[1]);
    point[2] = static_cast<float>(static_cast<double>(rz) * offset + point[2]);
}

// 0x00551300, slot 151: the room ahead, a float: 16 or, with a path and the
// automata flag 0x10, the distance to its last point less the radius (0
// below 1.0); 16 or, with +0xB0 and the flag 8, the distance to +0xB0's
// point (its slot 66) less the radius and 3. The smaller (ties and
// unordered the second).
float __fastcall ClearAhead(char* self, void*)
{
    static bool logged;
    FirstCall("ClearAhead", logged);
    const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
    float ahead = 16.0f, front = 16.0f;
    float point[3] = {g, g, g};
    float position[3] = {g, g, g};
    Virtual<std::uint32_t>(self, slot::kGetPosition, static_cast<float*>(position));
    if (At<void*>(self, field::kPath) && Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x10u)) {
        const float* const last = reinterpret_cast<const float*>(At<char**>(self, field::kPath)[1]) - 3;
        point[0] = static_cast<float>(static_cast<double>(last[0]) - position[0]);
        point[1] = static_cast<float>(static_cast<double>(last[1]) - position[1]);
        const double dz = static_cast<double>(last[2]) - position[2];
        point[2] = static_cast<float>(dz);
        ahead = static_cast<float>(std::sqrt((dz * point[2] + static_cast<double>(point[1]) * point[1]) +
                                             static_cast<double>(point[0]) * point[0]));
        const double gap = static_cast<double>(ahead) - Virtual<double>(self, slot::kGetRadius);
        ahead = static_cast<float>(gap);
        if (gap < Global<float>(addr::SC4_FloatOneA81228))
            ahead = 0.0f;
    }
    void* const object = At<void*>(self, field::kObjectB0);
    if (object && Virtual<std::uint8_t>(self, slot::kFlag8)) {
        Virtual<std::uint32_t>(At<void*>(self, field::kObjectB0), slot::kFrontPoint, static_cast<float*>(point));
        const double dx = static_cast<double>(point[0]) - position[0];
        const double dy = static_cast<double>(point[1]) - position[1];
        const double dz = static_cast<double>(point[2]) - position[2];
        point[0] = static_cast<float>(dx);
        point[1] = static_cast<float>(dy);
        point[2] = static_cast<float>(dz);
        front = static_cast<float>(std::sqrt((dz * dz + dy * dy) + dx * dx));
        front = static_cast<float>((static_cast<double>(front) - Virtual<double>(self, slot::kGetRadius)) -
                                   Global<float>(addr::cSC4AutomatonBase_FrontGap));
    }
    return ahead < front ? ahead : front;
}

// 0x0054FDC0, slot 129 (frame seconds), ret 4: the step (slot 125 x the
// smoothed speed x the seconds). With slot 88, an empty route and +0x204
// deque and the automata flag 0x20 slot 79 and true. Else slot 161, the room
// ahead (slot 151; below 16 with +0xB0 SetSpeed to +0xB0's speed at 0.5),
// the step at most that; at a step not above 0 the flag 0x20 set and, when
// minus the radius is above the step and the flag 0x40 is off, Remove(false)
// and false. Else slot 133 along the path into the destination (when false
// the flag 4 set and slot 102 (4, 0)), the destination kept inside the city
// (slots 89 and 90 less 0.5); within 0.001 in x and z slot 16 (destination,
// null) and the flag 0x20 set, else the flag cleared, the height from the
// occupant unless the network part is flat, the direction (slot 155) and
// slot 16. True.
std::uint8_t __fastcall MoveOnPath(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("MoveOnPath", logged);
    const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
    float heading[3] = {g, g, g};
    Virtual<void*>(City(), city::kTrafficNetwork);
    Virtual<void*>(self, slot::kManager);
    const float speed = static_cast<float>(Virtual<double>(self, slot::kSmoothedNow));
    float step = static_cast<float>((Virtual<double>(self, slot::kSlot125) * speed) * seconds);
    char* const route = self + field::kRoute;
    char* const deque204 = self + field::kDeque204;
    if (Virtual<std::uint8_t>(self, slot::kSlot88) &&
        At<char*>(route, deque::kFinish + deque::kCurrent) == At<char*>(route, deque::kStart + deque::kCurrent) &&
        At<char*>(deque204, deque::kFinish + deque::kCurrent) ==
            At<char*>(deque204, deque::kStart + deque::kCurrent) &&
        Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x20u)) {
        Virtual<std::uint32_t>(self, slot::kSlot79);
        return 1;
    }
    Virtual<std::uint32_t>(self, slot::kSlot161);
    const float clear = static_cast<float>(Virtual<double>(self, slot::kClearAhead));
    void* const front = At<void*>(self, field::kObjectB0);
    if (front && clear < Global<float>(addr::SC4_FloatSixteen)) {
        const float target = static_cast<float>(Virtual<double>(front, slot::kSmoothedNow));
        Virtual<std::uint32_t>(self, slot::kSetSpeed, target, 0.5f);
    }
    if (!(step < clear))
        step = clear;
    if (!(step > Zero())) {
        Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 1u);
        const double radius = Virtual<double>(self, slot::kGetRadius);
        if (!(-radius > step))
            return 1;
        if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x40u))
            return 1;
        Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        return 0;
    }
    float* const position = &At<float>(self, field::kPosition);
    float* const destination = &At<float>(self, field::kDestination);
    if (!Virtual<std::uint8_t>(self, slot::kAdvanceOnPath, step, position, destination, 0u, 0u, 0u)) {
        Virtual<std::uint32_t>(self, slot::kSetFlag, 4u, 1u);
        Virtual<std::uint32_t>(self, slot::kSlot102, 4u, 0u);
    }
    auto clamp = [](float value, double most) {
        if (value < Zero())
            return static_cast<double>(Zero());
        if (value > most)
            return most;
        return static_cast<double>(value);
    };
    const double width = Virtual<double>(City(), city::kWidth) - Global<float>(addr::SC4_FloatHalf);
    destination[0] = static_cast<float>(clamp(destination[0], width));
    const double depth = Virtual<double>(City(), city::kDepth) - Global<float>(addr::SC4_FloatHalf);
    destination[2] = static_cast<float>(clamp(destination[2], depth));
    const double dx = static_cast<double>(destination[0]) - position[0];
    const double dz = static_cast<double>(destination[2]) - position[2];
    if (!(std::sqrt(dz * dz + dx * dx) > Global<float>(addr::cSC4AutomatonBase_ArriveDistance))) {
        Virtual<std::uint32_t>(self, slot::kSlot16, destination, 0u);
        Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 1u);
        return 1;
    }
    Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 0u);
    if (!Virtual<std::uint8_t>(At<void*>(self, field::kNetwork), part::kFlat))
        destination[1] = static_cast<float>(
            Virtual<double>(At<void*>(self, field::kOccupant), kOccupantHeight, destination[0], destination[2]));
    Virtual<std::uint32_t>(self, slot::kDirectionTo, static_cast<const float*>(destination),
                           static_cast<float*>(heading));
    Virtual<std::uint32_t>(self, slot::kSlot16, destination, static_cast<float*>(heading));
    return 1;
}

// 0x00550190, slot 130 (frame seconds), ret 4: straight to the destination
// (the position when any of its coordinates is below 0; the y difference is
// the destination's y less itself). At a distance or step not above 0, or a
// move not below the distance (0 when the automata flag 8 is on and slot
// 123 finds a vehicle within 1), the flags 0x20 and 4 set, slot 102 (4, 0)
// and true. Else the flag 0x20 cleared and the new point, false when below
// 0 or not inside the city (slots 89, 90) in x or z; the direction (slot
// 155); unless the automata flag 0x20000 is on the traffic network map's
// slot 44 (position, point, 2 with slot 141 else 0xC49): when no Remove
// (false), else the height from the map (slot 18). Slot 16 and true.
std::uint8_t __fastcall MoveToDestination(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("MoveToDestination", logged);
    float* const position = &At<float>(self, field::kPosition);
    float* const destination = &At<float>(self, field::kDestination);
    const float px = position[0], py = position[1];
    const float pz = position[2];
    const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
    float point[3] = {g, g, g};
    float heading[3] = {0.0f, 0.0f, 0.0f};
    const float zero = Zero();
    if (destination[0] < zero || destination[1] < zero || destination[2] < zero) {
        destination[0] = px;
        destination[2] = pz;
        destination[1] = py;
    }
    const float a = static_cast<float>(static_cast<double>(destination[0]) - px);
    const double y = destination[1];
    const float b = static_cast<float>(y - y);
    const double dz = static_cast<double>(destination[2]) - pz;
    const float c = static_cast<float>(dz);
    const float length =
        static_cast<float>(std::sqrt((dz * c + static_cast<double>(b) * b) + static_cast<double>(a) * a));
    const float rate = static_cast<float>(Virtual<double>(self, slot::kSlot125));
    const float step = static_cast<float>((Virtual<double>(self, slot::kSmoothedNow) * rate) * seconds);
    auto arrive = [&] {
        Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 1u);
        Virtual<std::uint32_t>(self, slot::kSetFlag, 4u, 1u);
        Virtual<std::uint32_t>(self, slot::kSlot102, 4u, 0u);
        return static_cast<std::uint8_t>(1);
    };
    if (!(length > zero) || !(step > zero))
        return arrive();
    double limit = length;
    if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 8u) && Virtual<void*>(self, slot::kNearestVehicle, 1.0f))
        limit = zero;
    const double inverse = One() / static_cast<double>(length);
    const double ax = a * inverse;
    const float by = static_cast<float>(b * inverse);
    const float cz = static_cast<float>(c * inverse);
    const float mx = static_cast<float>(ax * step);
    const float my = static_cast<float>(by * static_cast<double>(step));
    const double mzu = cz * static_cast<double>(step);
    const float mz = static_cast<float>(mzu);
    const double moved = std::sqrt((mzu * mz + static_cast<double>(my) * my) + static_cast<double>(mx) * mx);
    if (moved >= limit)
        return arrive();
    Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 0u);
    const double nx = static_cast<double>(mx) + position[0];
    const double ny = static_cast<double>(my) + position[1];
    const float nz = static_cast<float>(static_cast<double>(mz) + position[2]);
    point[2] = nz;
    point[0] = static_cast<float>(nx);
    point[1] = static_cast<float>(ny);
    if (nx < zero || nz < zero)
        return 0;
    if (Virtual<double>(City(), city::kWidth) <= point[0])
        return 0;
    if (Virtual<double>(City(), city::kDepth) <= point[2])
        return 0;
    Virtual<std::uint32_t>(self, slot::kDirectionTo, static_cast<const float*>(point), static_cast<float*>(heading));
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x20000u)) {
        const std::uint32_t kind = Virtual<std::uint8_t>(self, slot::kSlot141) ? 2u : 0xC49u;
        if (!Virtual<std::uint8_t>(map, map::kCanGo, static_cast<float*>(position), static_cast<float*>(point), kind))
            Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        else
            point[1] = static_cast<float>(Virtual<double>(map, map::kHeight, point[0], point[2]));
    }
    Virtual<std::uint32_t>(self, slot::kSlot16, static_cast<float*>(point), static_cast<float*>(heading));
    return 1;
}

// The x87 sum of a direction's squares as slots 134 and 170 add them
// ((x * x + y * y) + z * z) and the direction normalised by it.
void Normalise(float* v)
{
    const double x = v[0], y = v[1], z = v[2];
    const double inverse = One() / std::sqrt((x * x + y * y) + z * z);
    v[0] = static_cast<float>(inverse * v[0]);
    v[1] = static_cast<float>(inverse * v[1]);
    v[2] = static_cast<float>(inverse * v[2]);
}

// 0x00550B60, slot 134: false at the core's default point (within 1e-4 in
// each coordinate); else the traffic network map's occupant at the
// position's cell (city slot 95, map slot 11) and its network part (false
// without either); the best side of +0xA4 (normalised; slot 171 on the
// occupant) when slot 122 answers, or when the automata flag 0x4000 is off,
// +0x98 is set and +0xA4 is longer than 1e-5; the part's path from the
// position (slot 5 with the vehicle manager's slot 68, 0xFF, the side, an
// out word and 0; false without one); slot 132 (occupant, the out word,
// path, 1); +0x1FC the part's index of the position on the path (slot 19,
// not below 0); slot 16 (the position, the unit vector of the path's first
// segment, else of the whole path, else of the heading +0x58). True.
std::uint8_t __fastcall PlaceOnNetwork(char* self, void*)
{
    static bool logged;
    FirstCall("PlaceOnNetwork", logged);
    float* const position = &At<float>(self, field::kPosition);
    const float* const unset = reinterpret_cast<const float*>(addr::cSC4AutomatonCore_DefaultPoint);
    const float within = Global<float>(addr::cSC4AutomatonBase_PlaceEpsilon);
    if (std::fabs(static_cast<double>(position[0]) - unset[0]) < within &&
        std::fabs(static_cast<double>(position[1]) - unset[1]) < within &&
        std::fabs(static_cast<double>(position[2]) - unset[2]) < within)
        return 0;
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    void* const vehicles = Virtual<void*>(self, slot::kManager);
    const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
    float heading[3] = {g, g, g};
    float point[3] = {g, g, g};
    std::int32_t cellX = 0, cellZ = 0; // stack garbage in the game; city slot 95 sets them
    Virtual<std::uint32_t>(City(), city::kCellOf, position[0], position[2], &cellX, &cellZ);
    void* const occupant = Virtual<void*>(map, map::kOccupantAt, cellX, cellZ);
    if (!occupant)
        return 0;
    void* const network = Virtual<void*>(occupant, kNetworkPartSlot);
    if (!network)
        return 0;
    const std::uint32_t kind = Virtual<std::uint32_t>(vehicles, manager::kKind, self);
    std::uint32_t side = 0xFFu; // a byte with three bytes of the game's stack above it
    float* const v = &At<float>(self, field::kVectorA4);
    bool best = false;
    if (Virtual<std::uint8_t>(self, slot::kSlot122, v)) {
        best = true;
    } else if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x4000u) && At<std::uint32_t>(self, field::k98)) {
        const double x = v[0], y = v[1], z = v[2];
        best = std::sqrt((x * x + y * y) + z * z) > Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon);
    }
    if (best) {
        Normalise(v);
        Virtual<std::uint32_t>(self, slot::kBestDirection, occupant, static_cast<const float*>(v),
                               reinterpret_cast<std::uint8_t*>(&side), 1u);
    }
    std::uint32_t found = 0; // stack garbage in the game; the part's slot 5 sets it
    char** const path = Virtual<char**>(network, part::kFindPath, kind, 0xFFu, side, static_cast<float*>(position),
                                        &found, 0u);
    if (!path)
        return 0;
    Virtual<std::uint32_t>(self, slot::kSlot132, occupant, found, path, 1u);
    CopyThree(point, position);
    const std::int32_t index = Virtual<std::int32_t>(network, part::kIndexOf, static_cast<float*>(point), path);
    At<std::int32_t>(self, field::kPathIndex) = index < 0 ? 0 : index;
    const float* const points = reinterpret_cast<const float*>(path[0]);
    if (static_cast<std::uint32_t>(PointCount(path)) <= 1) {
        CopyThree(heading, &At<float>(self, field::kHeading));
    } else {
        const float epsilon = Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon);
        // The x87 keeps x unrounded; y and z go through floats.
        auto length = [](float x, float y, float z) {
            return std::sqrt((static_cast<double>(z) * z + static_cast<double>(y) * y) + static_cast<double>(x) * x);
        };
        double xv = static_cast<double>(points[3]) - points[0];
        float yv = static_cast<float>(static_cast<double>(points[4]) - points[1]);
        float zv = static_cast<float>(static_cast<double>(points[5]) - points[2]);
        if (length(static_cast<float>(xv), yv, zv) < epsilon) {
            const float* const last = reinterpret_cast<const float*>(path[1]) - 3;
            xv = static_cast<double>(last[0]) - points[0];
            yv = static_cast<float>(static_cast<double>(last[1]) - points[1]);
            zv = static_cast<float>(static_cast<double>(last[2]) - points[2]);
            if (length(static_cast<float>(xv), yv, zv) < epsilon) {
                yv = At<float>(self, field::kHeading + 4);
                xv = At<float>(self, field::kHeading);
                zv = At<float>(self, field::kHeading + 8);
            }
        }
        const double inverse =
            One() / std::sqrt((static_cast<double>(zv) * zv + static_cast<double>(yv) * yv) + xv * xv);
        heading[0] = static_cast<float>(inverse * xv);
        heading[1] = static_cast<float>(yv * inverse);
        heading[2] = static_cast<float>(zv * inverse);
    }
    Virtual<std::uint32_t>(self, slot::kSlot16, static_cast<float*>(point), static_cast<float*>(heading));
    return 1;
}

// 0x00554AB0, slot 142 (now), ret 4: false without the vehicle manager; the
// +0x204 deque released (0x00554640), slot 90 (1), the automata flag 0x2000
// cleared; true with now. Else the route's first cell popped (0x00551F70),
// the side towards the next one (1 at a lower z, 3 at a higher, else 0 at a
// lower x, else 2; 0xFF without one), and of the traffic network map's
// occupants at the cell (slot 14, the nodes' +0x54) the first whose network
// part (slot 49) finds a path (slot 9 with the vehicle manager's slot 68,
// 0xFF, the side, the automata's generator, an out word and 0): slot 132
// (occupant, the out word, path, 1); the heading the path's unit vector
// first to last ((0, 0, 1) at 1e-5 or less); the point the path's first, or
// with the route now empty the part's end point (slot 22) and the flag 4
// set; slot 16 (point, heading) and true. False when none does.
std::uint8_t __fastcall NextRouteCell(char* self, void*, std::uint32_t now)
{
    static bool logged;
    FirstCall("NextRouteCell", logged);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    void* const vehicles = Virtual<void*>(self, slot::kManager);
    if (!vehicles)
        return 0;
    CallThis<void>(addr::cSC4AutomatonBase_ReleaseDeque204, self);
    Virtual<std::uint32_t>(self, slot::kSlot90, 1u);
    Virtual<std::uint32_t>(self, slot::kSetFlag, 0x2000u, 0u);
    if (static_cast<std::uint8_t>(now))
        return 1;
    const float g = Global<float>(addr::cSC4AutomatonBase_PointInit);
    float heading[3] = {g, g, g};
    float point[3] = {g, g, g};
    char* const route = self + field::kRoute;
    const std::uint8_t* const cell = At<std::uint8_t*>(route, deque::kStart + deque::kCurrent);
    const std::uint32_t x = cell[0], z = cell[1];
    std::uint32_t side = 0xFFu; // a byte with three bytes of the game's stack above it
    CallThis<char*>(addr::SC4RouteDeque_PopFront, route);
    const std::uint8_t* const next = At<std::uint8_t*>(route, deque::kStart + deque::kCurrent);
    if (At<std::uint8_t*>(route, deque::kFinish + deque::kCurrent) != next) {
        const std::int32_t dz = static_cast<std::int32_t>(next[1]) - static_cast<std::int32_t>(z);
        if (dz < 0)
            side = 1;
        else if (dz > 0)
            side = 3;
        else
            side = static_cast<std::int32_t>(next[0]) - static_cast<std::int32_t>(x) < 0 ? 0u : 2u;
    }
    std::uint32_t found = 0; // stack garbage in the game; the part's slot 9 sets it
    for (char* node = Virtual<char*>(map, map::kCellList, x, z); node; node = At<char*>(node, 0x54)) {
        void* const occupant = At<void*>(node, 0);
        if (!occupant)
            continue;
        void* const network = Virtual<void*>(occupant, kNetworkPartSlot);
        if (!network)
            continue;
        const std::uint32_t kind = Virtual<std::uint32_t>(vehicles, manager::kKind, self);
        char** const path = Virtual<char**>(network, part::kFindFrom, kind, 0xFFu, side,
                                            static_cast<std::uint32_t>(addr::cSC4AutomatonCore_Random), &found, 0u);
        if (!path)
            continue;
        Virtual<std::uint32_t>(self, slot::kSlot132, occupant, found, path, 1u);
        const float* const first = reinterpret_cast<const float*>(path[0]);
        const float* const last = reinterpret_cast<const float*>(path[1]) - 3;
        const double dx = static_cast<double>(last[0]) - first[0];
        const float dy = static_cast<float>(static_cast<double>(last[1]) - first[1]);
        const double dz = static_cast<double>(last[2]) - first[2];
        const double length = std::sqrt((dz * dz + static_cast<double>(dy) * dy) + dx * dx);
        if (length > Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon)) {
            const double inverse = One() / length;
            heading[0] = static_cast<float>(dx * inverse);
            heading[1] = static_cast<float>(dy * inverse);
            heading[2] = static_cast<float>(dz * inverse);
        } else {
            heading[0] = 0.0f;
            heading[1] = 0.0f;
            heading[2] = 1.0f;
        }
        if (At<char*>(self, field::kRoute + deque::kFinish + deque::kCurrent) ==
            At<char*>(self, field::kRoute + deque::kStart + deque::kCurrent)) {
            Virtual<std::uint32_t>(network, part::kEndPoint, path, static_cast<float*>(point));
            Virtual<std::uint32_t>(self, slot::kSetFlag, 4u, 1u);
        } else {
            CopyThree(point, first);
        }
        Virtual<std::uint32_t>(self, slot::kSlot16, static_cast<float*>(point), static_cast<float*>(heading));
        return 1;
    }
    return 0;
}

// ====================================================================================
// Part vb4: the states on frames

// The current state: the last of the core's stack (+0xB4..+0xB8; not
// tested for elements where the game does not).
char* CurrentState(char* self) { return At<char**>(self, field::kStatesEnd)[-1]; }

// The +0x204 deque's size as the asm counts it: (the blocks between less one)
// x 10, plus the finish's block to its current and the start's from its
// current, each a signed division by 12.
std::uint32_t StepCount(char* d)
{
    const std::int32_t nodes =
        Difference(At<char**>(d, deque::kFinish + deque::kNode), At<char**>(d, deque::kStart + deque::kNode)) >> 2;
    const std::int32_t inFinish =
        Difference(At<char*>(d, deque::kFinish + deque::kCurrent), At<char*>(d, deque::kFinish + deque::kFirst)) / 12;
    const std::int32_t inStart =
        Difference(At<char*>(d, deque::kStart + deque::kLast), At<char*>(d, deque::kStart + deque::kCurrent)) / 12;
    return static_cast<std::uint32_t>(nodes - 1) * 10u + static_cast<std::uint32_t>(inFinish) +
           static_cast<std::uint32_t>(inStart);
}

bool DequeEmpty(char* d)
{
    return At<char*>(d, deque::kFinish + deque::kCurrent) == At<char*>(d, deque::kStart + deque::kCurrent);
}

// SC4RefPtr's assignment of an element's or a field's object (the new one
// referenced first, the old one released).
void AssignReference(void** at, void* object)
{
    void* const old = *at;
    if (object == old)
        return;
    if (object)
        AddRef(object);
    *at = object;
    if (old)
        Release(old);
}

// A step on the +0x204 deque (0xC bytes): the occupant, its key, the byte.
struct Step {
    void* occupant;
    std::uint32_t key;
    std::uint8_t last;
    std::uint8_t unused[3];
};

// 0x00504C40, on a +0x204 deque iterator: ++ (blocks of 0x78 bytes). Answers
// the iterator.
DequeIterator* __fastcall StepIncrement(DequeIterator* it, void*)
{
    static bool logged;
    FirstCall("StepIncrement", logged);
    it->current += 0xC;
    if (it->current == it->last) {
        ++it->node;
        it->first = *it->node;
        it->last = it->first + 0x78;
        it->current = it->first;
    }
    return it;
}

// 0x00553720, on the +0x204 deque: pop_front; the element's object released
// (not cleared); at a block's last element the block freed (SC4::Free) and
// the start on the next node's block.
void __fastcall StepPopFront(char* d, void*)
{
    static bool logged;
    FirstCall("StepPopFront", logged);
    char*& current = At<char*>(d, deque::kStart + deque::kCurrent);
    void* const object = At<void*>(current, 0);
    if (current != At<char*>(d, deque::kStart + deque::kLast) - 0xC) {
        if (object)
            Release(object);
        current += 0xC;
        return;
    }
    if (object)
        Release(object);
    char* const first = At<char*>(d, deque::kStart + deque::kFirst);
    if (first)
        CallCdecl<void>(addr::SC4_Free, first);
    char** const node = ++At<char**>(d, deque::kStart + deque::kNode);
    At<char*>(d, deque::kStart + deque::kFirst) = *node;
    At<char*>(d, deque::kStart + deque::kLast) = *node + 0x78;
    current = At<char*>(d, deque::kStart + deque::kFirst);
}

// 0x0054E380, on a 0x5C-byte entry: zeroed, +0x48..+0x50 the point start
// (0x00B2293C). Answers the entry.
char* __fastcall InitEntry(char* entry, void*)
{
    static bool logged;
    FirstCall("InitEntry", logged);
    for (const std::size_t at : {0x0u, 0x4u, 0x8u, 0xCu, 0x20u, 0x24u, 0x30u, 0x34u, 0x40u, 0x44u})
        At<std::uint32_t>(entry, at) = 0;
    At<std::uint32_t>(entry, 0x48) = Global<std::uint32_t>(addr::cSC4AutomatonBase_PointInit);
    At<std::uint32_t>(entry, 0x4C) = Global<std::uint32_t>(addr::cSC4AutomatonBase_PointInit);
    At<std::uint32_t>(entry, 0x50) = Global<std::uint32_t>(addr::cSC4AutomatonBase_PointInit);
    At<std::uint32_t>(entry, 0x54) = 0;
    At<std::uint32_t>(entry, 0x58) = 0;
    return entry;
}

// 0x0054ECC0, stdcall (record), ret 4: with the byte +0x30 and an object
// +0x2C, the city's slot 47's slot 14 (the object) and the byte cleared; the
// object cleared and released; +0x34 zero. AL 1.
std::uint8_t __stdcall ClearRecord(char* record)
{
    static bool logged;
    FirstCall("ClearRecord", logged);
    if (At<std::uint8_t>(record, 0x30) && At<void*>(record, 0x2C)) {
        void* const lights = Virtual<void*>(City(), kCityLights);
        Virtual<std::uint32_t>(lights, 0x38, At<void*>(record, 0x2C));
        At<std::uint8_t>(record, 0x30) = 0;
    }
    void* const object = At<void*>(record, 0x2C);
    if (object) {
        At<void*>(record, 0x2C) = nullptr;
        Release(object);
    }
    At<std::uint32_t>(record, 0x34) = 0;
    return 1;
}

// 0x00552B80, on a record list (record), ret 4: the node holding the record
// (node + 8): 0x0054ECC0, unlinked, +0x34's object released, the node freed
// (SC4::Free). AL 1, found or not.
std::uint8_t __fastcall EraseRecord(char** list, void*, char* record)
{
    static bool logged;
    FirstCall("EraseRecord", logged);
    char* const head = *list;
    char* node = At<char*>(head, 0);
    if (node == head)
        return 1;
    while (record != node + 8) {
        node = At<char*>(node, 0);
        if (node == head)
            return 1;
    }
    reinterpret_cast<std::uint8_t(__stdcall*)(char*)>(addr::cSC4VehPedManagerBase_ClearRecord)(node + 8);
    char* const previous = At<char*>(node, 4);
    char* const next = At<char*>(node, 0);
    At<char*>(previous, 0) = next;
    At<char*>(next, 4) = previous;
    if (void* const object = At<void*>(node, 0x34))
        Release(object);
    CallCdecl<void>(addr::SC4_Free, node);
    return 1;
}

// 0x0054E050, slot 72 (a word, frame seconds), ret 8: the smoothed value's
// elapsed time (+0x1AC) on by seconds x 0.001 while below its duration; at
// the duration the target becomes the value and both are zero. Slots 83 and
// 103 (seconds); with slot 86 and not slot 109 (seconds): unless slot 87, the
// move (slot 129 with an occupant, a network part and a path and without the
// automata flag 0x2000, else slot 130); with slot 87, not gone (slot 85) and
// not slot 165, Remove(false); slot 105. Then slot 102 (0, 0). Answers the
// move's AL, else 1.
std::uint8_t __fastcall Frame(char* self, void*, std::uint32_t, float seconds)
{
    static bool logged;
    FirstCall("Frame", logged);
    std::uint8_t answer = 1;
    float& elapsed = At<float>(self, field::kSmoothedElapsed);
    const float duration = At<float>(self, field::kSmoothedDuration);
    if (elapsed < duration) {
        const double now = static_cast<double>(seconds) * Global<float>(addr::cSC4AutomatonBase_Milli) + elapsed;
        elapsed = static_cast<float>(now);
        if (now >= At<float>(self, field::kSmoothedDuration)) {
            const std::uint32_t target = At<std::uint32_t>(self, field::kSmoothedTarget);
            At<std::uint32_t>(self, field::kSmoothedElapsed) = 0;
            At<std::uint32_t>(self, field::kSmoothedDuration) = 0;
            At<std::uint32_t>(self, field::kSmoothed) = target;
        }
    }
    Virtual<std::uint32_t>(self, slot::kSlot83, seconds);
    Virtual<std::uint32_t>(self, slot::kSlot103, seconds);
    if (Virtual<std::uint8_t>(self, slot::kSlot86) && !Virtual<std::uint8_t>(self, slot::kStateFrame, seconds)) {
        if (!Virtual<std::uint8_t>(self, slot::kSlot87)) {
            if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x2000u) && At<void*>(self, field::kOccupant) &&
                At<void*>(self, field::kNetwork) && At<void*>(self, field::kPath))
                answer = Virtual<std::uint8_t>(self, slot::kMoveOnPath, seconds);
            else
                answer = Virtual<std::uint8_t>(self, slot::kMoveStraight, seconds);
        }
        if (Virtual<std::uint8_t>(self, slot::kSlot87) && !Virtual<std::uint8_t>(self, slot::kIsGone) &&
            !Virtual<std::uint8_t>(self, slot::kNextStep))
            Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        Virtual<std::uint32_t>(self, slot::kSlot105);
    }
    Virtual<std::uint32_t>(self, slot::kSlot102, 0u, 0u);
    return answer;
}

// 0x00550090, slot 109 (frame seconds), ret 4: the current state's frame,
// slot 163 for the type 0xD, slot 164 for 0x1E; else (or no state) the
// core's 0x00508AA0. Tail calls in the game: answers the callee's EAX.
std::uint32_t __fastcall StateFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("StateFrame", logged);
    if (At<char*>(self, field::kStates) != At<char*>(self, field::kStatesEnd)) {
        const std::uint32_t type = At<std::uint32_t>(CurrentState(self), state::kType);
        if (type == state::kWait)
            return Virtual<std::uint32_t>(self, slot::kWaitFrame, seconds);
        if (type == state::kDrive)
            return Virtual<std::uint32_t>(self, slot::kDriveFrame, seconds);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_StateFrame, self, seconds);
}

// 0x005546E0, slot 110 (state), ret 4: entering a state. The type 0x10:
// the vehicle manager's record list given a record (0x00554160 with 0, 0, 5
// and the occupant part, slot 5) into +0x64; the entries' object
// (0x00585110) into +0x6C and, when there is one, an entry at the position
// (0x0054E380, then 0x00584250 with it and 0) into +0x70; the automata flag
// 0x40 cleared, the route and the +0x204 deque emptied; the core's
// 0x00511450 (state); with an occupant answering its slot 9 for 0x403C1004:
// +0x78 from the occupant (SC4RefPtr::AssignFrom) when its slot 8 answers
// 0x80000, the byte +0x228 zero and +0x8C -1; then the core's 0x00511450
// again. The type 0x1E: SetSpeed (0, 0.5) and the core's. Else the core's.
// Answers the last call's EAX.
std::uint32_t __fastcall EnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("EnterState", logged);
    if (entered && At<std::uint32_t>(entered, state::kType) == state::kParked) {
        void* const occupantPart = Virtual<void*>(self, slot::kAsOccupant);
        char* const vehicles = Virtual<char*>(self, slot::kManager);
        At<char*>(entered, state::kRecord) =
            CallThis<char*>(addr::cSC4VehPedManagerBase_PushRecordListBack, vehicles + manager::kRecordList, 0u, 0u,
                            5u, occupantPart);
        char* const entries = CallCdecl<char*>(addr::SC4AutomataEntries_Get);
        At<char*>(entered, state::kEntries) = entries;
        if (entries) {
            alignas(4) char entry[0x60] = {}; // the game's three bytes after +0x3C are stack garbage
            CallThis<char*>(addr::cSC4AutomatonBase_InitEntry, entry);
            At<std::uint32_t>(entry, 0x00) = At<std::uint32_t>(self, field::kPosition);
            At<std::uint32_t>(entry, 0x04) = At<std::uint32_t>(self, field::kPosition + 8);
            At<std::uint32_t>(entry, 0x10) = At<std::uint32_t>(self, field::kPosition + 4);
            At<std::uint32_t>(entry, 0x14) = 0;
            At<std::uint32_t>(entry, 0x18) = 0;
            At<std::uint32_t>(entry, 0x1C) = 0;
            At<std::uint32_t>(entry, 0x20) = 0x3F800000u;
            At<std::uint32_t>(entry, 0x24) = 0x3F800000u;
            At<std::uint32_t>(entry, 0x28) = 0x3F800000u;
            At<std::uint32_t>(entry, 0x2C) = 0x33D6BF95u;
            At<std::uint32_t>(entry, 0x38) = 0;
            At<std::uint8_t>(entry, 0x3C) = 0;
            At<std::uint32_t>(entry, 0x5C) = 0;
            At<std::uint32_t>(entry, 0x40) = 0;
            At<std::uint32_t>(entry, 0x44) = 0;
            At<std::uint32_t>(entered, state::kEntry) =
                CallThis<std::uint32_t>(addr::SC4AutomataEntries_Add, At<char*>(entered, state::kEntries),
                                        static_cast<void*>(entry), 0u);
        }
        Virtual<std::uint32_t>(self, slot::kSetFlag, 0x40u, 0u);
        CallThis<void>(addr::SC4CellDeque_Clear, self + field::kRoute);
        CallThis<void>(addr::SC4AutomatonDeque204_Clear, self + field::kDeque204);
        CallThis<std::uint32_t>(addr::cSC4AutomatonCore_EnterState, self, entered);
        void* const occupant = At<void*>(self, field::kOccupant);
        if (occupant && Virtual<std::uint8_t>(occupant, kOccupantFlag9, 0x403C1004u)) {
            if (Virtual<std::uint8_t>(At<void*>(self, field::kOccupant), kOccupantFlag, 0x80000u))
                CallThis<std::uint32_t>(addr::SC4RefPtr_AssignFrom, entered + state::kRef78,
                                        static_cast<void*>(self + field::kOccupant));
            At<std::uint8_t>(entered, state::kByte228) = 0;
            At<std::uint32_t>(entered, state::kFloat8C) = Global<std::uint32_t>(addr::cSC4AutomatonBase_MinusOne);
        }
    } else if (entered && At<std::uint32_t>(entered, state::kType) == state::kDrive) {
        Virtual<std::uint32_t>(self, slot::kSetSpeed, 0.0f, 0.5f);
        return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_EnterState, self, entered);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_EnterState, self, entered);
}

// 0x005535D0, slot 111 (state), ret 4: leaving a state. The type 0xD: slot
// 160 and the automata flags 0x10 and 0x20 cleared; true. 0x1E: the same and
// the core's 0x0050E3F0 (state); true. 0x10: unless the flag 1, the flag
// 0x2000 cleared and slot 134; the records +0x64 and +0x68 erased from the
// vehicle manager's record list (0x00552B80) and cleared; +0x74's slot 4 (0)
// and its release; with +0x6C, +0x70's entry removed (0x00584B30) and
// cleared, the entries' object let go (0x00583860) and +0x6C cleared; then
// the core's. 0x15: unless the flag 1 slot 134; true. Else the core's. AL
// only where true.
std::uint32_t __fastcall LeaveState(char* self, void*, char* left)
{
    static bool logged;
    FirstCall("LeaveState", logged);
    if (left) {
        const std::uint32_t type = At<std::uint32_t>(left, state::kType);
        if (type == state::kWait) {
            Virtual<std::uint32_t>(self, slot::kGo);
            Virtual<std::uint32_t>(self, slot::kSetFlag, 0x10u, 0u);
            Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 0u);
            return 1;
        }
        if (type == state::kDrive) {
            Virtual<std::uint32_t>(self, slot::kGo);
            Virtual<std::uint32_t>(self, slot::kSetFlag, 0x10u, 0u);
            Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 0u);
            CallThis<std::uint32_t>(addr::cSC4AutomatonCore_LeaveState, self, left);
            return 1;
        }
        if (type == state::kParked) {
            if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 1u)) {
                Virtual<std::uint32_t>(self, slot::kSetFlag, 0x2000u, 0u);
                Virtual<std::uint8_t>(self, slot::kSlot134);
            }
            for (const std::size_t at : {state::kRecord, state::kRecord2}) {
                if (!At<char*>(left, at))
                    continue;
                if (char* const vehicles = Virtual<char*>(self, slot::kManager))
                    CallThis<std::uint8_t>(addr::cSC4VehPedManagerBase_EraseRecord, vehicles + manager::kRecordList,
                                           At<char*>(left, at));
                At<char*>(left, at) = nullptr;
            }
            if (At<void*>(left, state::kObject74)) {
                Virtual<std::uint32_t>(At<void*>(left, state::kObject74), 0x10, 0u);
                Release(At<void*>(left, state::kObject74));
            }
            if (At<char*>(left, state::kEntries)) {
                if (const std::uint32_t entry = At<std::uint32_t>(left, state::kEntry)) {
                    CallThis<std::uint32_t>(addr::SC4AutomataEntries_Remove, At<char*>(left, state::kEntries), entry);
                    At<std::uint32_t>(left, state::kEntry) = 0;
                }
                CallCdecl<void>(addr::SC4AutomataEntries_Release);
                At<char*>(left, state::kEntries) = nullptr;
            }
        } else if (type == state::kPlace) {
            if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 1u))
                Virtual<std::uint8_t>(self, slot::kSlot134);
            return 1;
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_LeaveState, self, left);
}

// 0x005559D0, slot 145 (route, next, now), ret 0xC: false for an empty
// route; else the route copied in (SC4RouteDeque's assignment 0x00532430);
// with next, false when slot 142 (now) is; slot 160 and true. AL only.
std::uint8_t __fastcall SetRoute(char* self, void*, char* route, std::uint32_t next, std::uint32_t now)
{
    static bool logged;
    FirstCall("SetRoute", logged);
    if (DequeEmpty(route))
        return 0;
    CallThis<char*>(addr::SC4RouteDeque_Assign, self + field::kRoute, route);
    if (static_cast<std::uint8_t>(next) && !Virtual<std::uint8_t>(self, slot::kNextRouteCell, now))
        return 0;
    Virtual<std::uint32_t>(self, slot::kGo);
    return 1;
}

// 0x00551480, slot 147 (occupant, depth), ret 8: the occupant (without one
// its own, slot 137, or with the automata flag 0x2000 the traffic network
// map's at its cell; none, nothing); of the automata the map has on it (slot
// 16, the list at +0x1C, the automaton at +8) every other one not with the
// flag 0x40, with the flag 2, not gone, without slot 57 (0x10) and without
// the flag 0x200000 whose occupant type (its part's slot 7) is its own:
// Remove(false) and slot 33 (0, 0x00B21C84 x 0.25). Then, at a depth not
// below 0, for each of the four sides the map's neighbour (slot 15 with the
// occupant, the side and 0, 0): slot 147 on it with the depth less one.
void __fastcall RemoveSame(char* self, void*, void* occupant, std::int32_t depth)
{
    static bool logged;
    FirstCall("RemoveSame", logged);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    if (!occupant) {
        occupant = Virtual<void*>(self, slot::kOccupantOf);
        if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x2000u)) {
            std::int32_t cellX = 0; // stack garbage in the game; city slot 95 sets it
            std::int32_t cellZ = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(occupant));
            Virtual<std::uint32_t>(City(), city::kCellOf, At<float>(self, field::kPosition),
                                   At<float>(self, field::kPosition + 8), &cellX, &cellZ);
            occupant = Virtual<void*>(map, map::kOccupantAt, cellX, cellZ);
        }
        if (!occupant)
            return;
    }
    char* const on = Virtual<char*>(map, map::kAutomataOn, occupant);
    if (!on)
        return;
    for (char* node = At<char*>(At<char*>(on, 0x1C), 0); node != At<char*>(on, 0x1C); node = At<char*>(node, 0)) {
        char* const other = At<char*>(node, 8);
        if (other == self || Virtual<std::uint8_t>(other, slot::kCheckFlag, 0x40u) ||
            !Virtual<std::uint8_t>(other, slot::kCheckFlag, 2u) || Virtual<std::uint8_t>(other, slot::kIsGone) ||
            Virtual<std::uint32_t>(other, slot::kSlot57, 0x10u) ||
            Virtual<std::uint8_t>(other, slot::kCheckFlag, 0x200000u))
            continue;
        const std::uint32_t type = Virtual<std::uint32_t>(self + field::kVtable4, slot::kPartType);
        if (Virtual<std::uint32_t>(other + field::kVtable4, slot::kPartType) != type)
            continue;
        Virtual<std::uint32_t>(other, slot::kRemove, 0u);
        Virtual<std::uint32_t>(other, slot::kFade, 0.0f,
                               static_cast<float>(static_cast<double>(Global<float>(addr::SC4AutomataTuning_Fade21C84)) *
                                                  Global<float>(addr::cSC4AutomatonBase_Quarter)));
    }
    if (depth < 0)
        return;
    // The side is the low byte of the occupant argument's stack slot: its
    // upper three bytes are the occupant pointer's.
    const std::uint32_t upper = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)) & 0xFFFFFF00u;
    for (std::uint32_t side = 0; side < 4; ++side) {
        void** const next = Virtual<void**>(map, map::kNeighbour, occupant, upper | side, 0u, 0u);
        if (next)
            Virtual<std::uint32_t>(self, slot::kRemoveSame, *next, depth - 1);
    }
}

// 0x00551C20, slot 148: the automaton ahead. With two steps or more on the
// +0x204 deque, the first the steps find (0x0054FC30 with each step's
// occupant and key, 0xFFF00FF, 0xFFFFF00). Else, on the occupant +0x1EC,
// 0x0054FC30 with +0x1F4 and the same masks; none there, 0x00B43D54's
// neighbour (slot 0x3C with the occupant, the side +0x1F4, 0 and the kind)
// and 0x0054FC30 on it with the opposite side (kind << 24 | side << 8 |
// 0xFF, 0xF00FF00 twice). Null without.
void* __fastcall FindAhead(char* self, void*)
{
    static bool logged;
    FirstCall("FindAhead", logged);
    using Nearest = void*(__thiscall*)(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    const auto nearest = reinterpret_cast<Nearest>(addr::cSC4AutomatonBase_NearestOnNetwork);
    char* const d = self + field::kDeque204;
    void* found = nullptr;
    if (StepCount(d) >= 2) {
        char* current = At<char*>(d, deque::kStart + deque::kCurrent);
        char* last = At<char*>(d, deque::kStart + deque::kLast);
        char** node = At<char**>(d, deque::kStart + deque::kNode);
        while (current != At<char*>(d, deque::kFinish + deque::kCurrent) && !found) {
            found = nearest(self, At<std::uint32_t>(current, 0), At<std::uint32_t>(current, 4), 0xFFF00FFu,
                            0xFFFFF00u);
            current += 0xC;
            if (current == last) {
                current = *++node;
                last = current + 0x78;
            }
        }
        return found;
    }
    void* const occupant = At<void*>(self, field::kOccupant);
    if (!occupant)
        return nullptr;
    found = nearest(self, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)),
                    At<std::uint32_t>(self, field::kGroups1F4), 0xFFF00FFu, 0xFFFFF00u);
    if (found)
        return found;
    const std::uint32_t side = At<std::uint8_t>(self, field::kBytes1F4); // three bytes of stack garbage above it
    const std::uint32_t kind = At<std::uint8_t>(self, field::kKind1F7) & 0xFu;
    void** const next = Virtual<void**>(Global<void*>(addr::SC4_Service43D54), kServiceNeighbour,
                                        At<void*>(self, field::kOccupant), side, 0u, kind);
    if (!next)
        return nullptr;
    const std::uint32_t key = (kind << 24) | (((side + 2) & 3u) << 8) | 0xFFu;
    return nearest(self, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(*next)), key, 0xF00FF00u,
                   0xF00FF00u);
}

// 0x0054E1A0, slot 161: the automaton ahead and the waiting. With the
// automata flag 8: +0xB0 = slot 148 (SC4RefPtr::Assign) when +0xB0 is gone
// (its flag 1) or, with +0xB0, the flag 0x10 is on. With the flag 0x800, or
// the occupant part's byte (slot 33) without 4 and the flag 0x200000: the
// flag 0x10 cleared and, without +0xB0, slot 160. Else nothing in the state
// 0xD (slot 59); a wait of 1.7 when slot 141 is off and the network part
// says so for the kind (its slot 13); otherwise the traffic network map's
// slot 33 (occupant, automaton, groups, the wait): with an answer the flag
// 0x10 set, slot 52 (0xD, 0, 0x00B21C74 x 1000, 0, 0, 0, 0) at 1 with the
// flag 0x20, and slot 159. Without: with the flag 0x10 and a wait below 0.7
// slot 159, else the flag 0x10 cleared and slot 160.
void __fastcall CheckAhead(char* self, void*)
{
    static bool logged;
    FirstCall("CheckAhead", logged);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 8u)) {
        void* const ahead = At<void*>(self, field::kObjectB0);
        if ((ahead && Virtual<std::uint8_t>(ahead, slot::kCheckFlag, 1u)) ||
            (At<void*>(self, field::kObjectB0) && Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x10u))) {
            void* const front = Virtual<void*>(self, slot::kSlot148);
            CallThis<void>(addr::SC4RefPtr_Assign, self + field::kObjectB0, front);
        }
    }
    if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x800u) ||
        (!(Virtual<std::uint32_t>(self + field::kVtable4, slot::kPartFlags) & 4u) &&
         Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x200000u))) {
        Virtual<std::uint32_t>(self, slot::kSetFlag, 0x10u, 0u);
        if (!At<void*>(self, field::kObjectB0))
            Virtual<std::uint32_t>(self, slot::kGo);
        return;
    }
    if (Virtual<std::uint32_t>(self, slot::kStateType) == state::kWait)
        return;
    float wait = 0.0f;
    void* const network = At<void*>(self, field::kNetwork);
    if (!Virtual<std::uint8_t>(self, slot::kSlot141) && network &&
        Virtual<std::uint8_t>(network, 0x34, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kKind1F7) & 0xFu))) {
        wait = 1.7f; // 0x3FD9999A
    } else {
        void* const occupant = At<void*>(self, field::kOccupant);
        const std::uint32_t groups = Virtual<std::uint32_t>(self, slot::kGroups);
        const std::uint32_t answer = Virtual<std::uint32_t>(map, map::kWaitOn, occupant, self, groups, &wait);
        if (answer) {
            Virtual<std::uint32_t>(self, slot::kSetFlag, 0x10u, 1u);
            if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x20u) && answer == 1) {
                const float time = static_cast<float>(
                    static_cast<double>(Global<float>(addr::SC4AutomataTuning_Signal4998C912)) *
                    Global<float>(addr::cSC4AutomatonBase_Thousand));
                Virtual<std::uint32_t>(self, slot::kPushState, state::kWait, 0u, time, 0u, 0u, 0u, 0u);
            }
            Virtual<std::uint32_t>(self, slot::kStop);
            return;
        }
    }
    if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x10u) && wait < Global<float>(addr::cSC4AutomatonBase_WaitGate)) {
        Virtual<std::uint32_t>(self, slot::kStop);
        return;
    }
    Virtual<std::uint32_t>(self, slot::kSetFlag, 0x10u, 0u);
    Virtual<std::uint32_t>(self, slot::kGo);
}

// 0x0054E410, slot 162: whether the queue ahead fits. From the automaton
// along the ones ahead (slot 48) while there are fewer than 50 and they have
// the automata flags 0x10 and 0x20: those without the flag 1 counted, the
// last of the occupant types 0xC7CC75F1 and 0x496E5783 (the occupant part's
// slot 7) kept. With one kept and a count above 2: its occupant (slot 137)
// and the traffic network map's object on it (slot 16), the vehicle
// manager's weights (slot 72 with them, +0x1E4 and slot 42's weight) and
// factor (slot 75); the weight of the kept one's groups (row byte 1, column
// byte 0) x the factor x 10 rounded (the x87 trick: less 0.5 plus 1.5 x
// 2^23 as a float, one more when exactly that) below the count: false.
// Otherwise (and without a manager) true. AL only.
std::uint8_t __fastcall QueueFits(char* self, void*)
{
    static bool logged;
    FirstCall("QueueFits", logged);
    char* const vehicles = Virtual<char*>(self, slot::kManager);
    if (!vehicles)
        return 1;
    std::uint32_t count = 0;
    char* kept = nullptr;
    for (char* at = self; at && count < 50 && Virtual<std::uint8_t>(at, slot::kCheckFlag, 0x10u) &&
                          Virtual<std::uint8_t>(at, slot::kCheckFlag, 0x20u);
         at = Virtual<char*>(at, slot::kNextAutomaton)) {
        if (!Virtual<std::uint8_t>(at, slot::kCheckFlag, 1u))
            ++count;
        if (Virtual<std::uint32_t>(Virtual<void*>(at, slot::kAsOccupant), slot::kPartType) == kVehicleType ||
            Virtual<std::uint32_t>(Virtual<void*>(at, slot::kAsOccupant), slot::kPartType) == 0x496E5783u)
            kept = at;
    }
    if (!kept || count <= 2)
        return 1;
    const double factor = Virtual<double>(vehicles, manager::kQueueFactor, At<std::uint32_t>(self, field::kState));
    void* const occupant = Virtual<void*>(kept, slot::kOccupantOf);
    if (!occupant)
        return 1;
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    void* const on = Virtual<void*>(map, map::kAutomataOn, occupant);
    const float weight =
        static_cast<float>(Virtual<double>(vehicles, manager::kStateWeight, At<std::uint32_t>(self, field::kState)));
    float table[16] = {}; // stack garbage in the game; the manager fills it
    Virtual<std::uint32_t>(vehicles, manager::kWeights, occupant, on, At<std::uint32_t>(self, field::kState), weight,
                           static_cast<float*>(table));
    const std::uint32_t row = Virtual<std::uint32_t>(kept, slot::kGroups);
    const std::uint32_t column = Virtual<std::uint32_t>(kept, slot::kGroups);
    const double v =
        static_cast<double>(table[(column & 0xFFu) + ((row >> 8) & 0xFFu) * 4]) * factor *
        Global<float>(addr::cSC4AutomatonBase_QueueScale);
    const float rounded = static_cast<float>((v - Global<float>(addr::SC4_FloatHalf)) +
                                             Global<float>(addr::cSC4AutomatonBase_RoundMagic));
    std::uint32_t limit;
    std::memcpy(&limit, &rounded, 4);
    limit += 0xB4C00000u;
    if (v == static_cast<double>(static_cast<std::int32_t>(limit + 1)))
        ++limit;
    return limit < count ? 0 : 1;
}

// 0x005500D0, slot 163 (frame seconds), ret 4: the state 0xD's frame. While
// the current state's timer (+0x10) is above 0: false without the automata
// flag 0x20; else the timer less the core's rate (0x00505AB0) x the seconds,
// true while that is above 0. At its end: slot 160, the flags 0x20 and 0x10
// cleared and the traffic network map's slot 33 (occupant, automaton,
// groups, a word holding the object's address); without an answer slot 53.
// False. AL only.
std::uint8_t __fastcall WaitFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("WaitFrame", logged);
    char* const current = CurrentState(self);
    float& timer = At<float>(current, state::kTimer);
    if (timer > Zero()) {
        if (!Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x20u))
            return 0;
        const double rate = CallThis<double>(addr::cSC4AutomatonCore_StateRate, self);
        const double left = timer - rate * seconds;
        timer = static_cast<float>(left);
        if (left > Zero())
            return 1;
    }
    Virtual<std::uint32_t>(self, slot::kGo);
    Virtual<std::uint32_t>(self, slot::kSetFlag, 0x20u, 0u);
    Virtual<std::uint32_t>(self, slot::kSetFlag, 0x10u, 0u);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    void* const occupant = At<void*>(self, field::kOccupant);
    // The game's out word is the stack slot ECX was pushed into: the object.
    std::uint32_t wait = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    const std::uint32_t groups = Virtual<std::uint32_t>(self, slot::kGroups);
    if (!Virtual<std::uint32_t>(map, map::kWaitOn, occupant, self, groups, &wait))
        Virtual<std::uint32_t>(self, slot::kPopState);
    return 0;
}

// 0x0054E3D0, slot 164 (frame seconds), ret 4: the state 0x1E's frame:
// slot 129 (seconds) while the smoothed value (0x004FA960) is above 0, then
// the core's 0x00508AA0 (seconds). AL 1.
std::uint8_t __fastcall DriveFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("DriveFrame", logged);
    if (CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, self + field::kSmoothed) > Zero())
        Virtual<std::uint8_t>(self, slot::kMoveOnPath, seconds);
    CallThis<std::uint32_t>(addr::cSC4AutomatonCore_StateFrame, self, seconds);
    return 1;
}

// 0x00553AA0, slot 165: the next step. False without the vehicle manager;
// false (nothing more) without the occupant +0x1EC or with the automata flag
// 0x2000. Slot 166; the first step's occupant cleared and released; when it
// was the last step (or none is left after) Remove(false) and false, the
// first step popped (0x00553720). Else from the path's last point (or the
// position, slot 140) each step in turn by slot 169 (the step, the point, an
// out key, an out distance): when one is found the step's key set, the
// answer true and the point the end of the occupant's network part's path
// for the key. The byte +0x1E8 counted down; slot 131 to the first step
// (occupant, key, true); when it is the last, slot 79 with slot 88, else
// Remove(false). AL only.
std::uint8_t __fastcall NextStep(char* self, void*)
{
    static bool logged;
    FirstCall("NextStep", logged);
    std::uint8_t answer = 0;
    if (!Virtual<void*>(self, slot::kManager))
        return 0;
    if (!At<void*>(self, field::kOccupant) || Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x2000u))
        return answer;
    Virtual<std::uint8_t>(self, slot::kFillAhead);
    char* const d = self + field::kDeque204;
    if (DequeEmpty(d)) {
        Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        return 0;
    }
    Step* const first = At<Step*>(d, deque::kStart + deque::kCurrent);
    if (void* const object = first->occupant) {
        first->occupant = nullptr;
        Release(object);
    }
    if (first->last) {
        Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        CallThis<void>(addr::SC4AutomatonDeque204_PopFront, d);
        return 0;
    }
    CallThis<void>(addr::SC4AutomatonDeque204_PopFront, d);
    if (DequeEmpty(d)) {
        Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        return 0;
    }
    const float* const from = At<char**>(self, field::kPath)
                                  ? reinterpret_cast<const float*>(At<char**>(self, field::kPath)[1]) - 3
                                  : Virtual<const float*>(self, slot::kPosition);
    float point[3];
    CopyThree(point, from);
    DequeIterator out = {};
    DequeIterator it = *CallThis<DequeIterator*>(addr::SC4CellDeque_Begin, d, &out);
    while (it.current != At<char*>(d, deque::kFinish + deque::kCurrent)) {
        std::uint32_t key = 0;   // stack garbage in the game; slot 169 sets it when true
        float distance = 0.0f;
        Step* const step = reinterpret_cast<Step*>(it.current);
        if (Virtual<std::uint8_t>(self, slot::kBestKey, it, static_cast<float*>(point), &key, &distance)) {
            step->key = key;
            answer = 1;
            void* const network = Virtual<void*>(step->occupant, kNetworkPartSlot);
            if (network) {
                char** const path = Virtual<char**>(network, part::kPath, key, 0u);
                if (path)
                    CopyThree(point, reinterpret_cast<const float*>(path[1]) - 3);
            }
        }
        CallThis<DequeIterator*>(addr::SC4AutomatonDeque204_Increment, &it);
    }
    std::uint8_t& countdown = At<std::uint8_t>(self, field::kByte1E8);
    if (countdown > 0)
        --countdown;
    if (DequeEmpty(d))
        return answer;
    Step* const next = At<Step*>(d, deque::kStart + deque::kCurrent);
    Virtual<std::uint8_t>(self, slot::kPathTo, next->occupant, next->key, 1u);
    if (!next->last)
        return answer;
    if (Virtual<std::uint8_t>(self, slot::kSlot88))
        Virtual<std::uint32_t>(self, slot::kSlot79);
    else
        Virtual<std::uint32_t>(self, slot::kRemove, 0u);
    return answer;
}

// 0x00554870, slot 166: the +0x204 deque filled ahead. Empty, it is given
// the occupant +0x1EC with the groups +0x1F4 (0x00554230). Then while it
// holds fewer than slot 175's byte plus one steps: the last step, when not
// marked last and with an occupant, extended by slot 168 (with the automata
// flag 0x40) or slot 167 into a new step pushed at the back; a step that
// cannot be extended is marked last and the answer is false. True when
// full or at a step marked last; false at a step without an occupant. AL
// only.
std::uint8_t __fastcall FillAhead(char* self, void*)
{
    static bool logged;
    FirstCall("FillAhead", logged);
    char* const d = self + field::kDeque204;
    std::uint8_t ok = 1;
    // One local for the steps: its key stays from the first push or the last
    // extension (stack garbage in the game when the deque was not empty).
    Step step{nullptr, 0, 0, {}};
    if (DequeEmpty(d)) {
        step.key = At<std::uint32_t>(self, field::kGroups1F4);
        void* const occupant = At<void*>(self, field::kOccupant);
        if (occupant) {
            AddRef(occupant);
            step.occupant = occupant;
        }
        step.last = 0;
        CallThis<void>(addr::SC4AutomatonDeque204_PushBack, d, &step);
        if (occupant)
            Release(occupant);
    }
    const std::uint32_t wanted = Virtual<std::uint32_t>(self, slot::kStaticByte) + 1;
    while (StepCount(d) < wanted) {
        if (!ok)
            return 0;
        char* back = At<char*>(d, deque::kFinish + deque::kCurrent);
        if (back == At<char*>(d, deque::kFinish + deque::kFirst))
            back = At<char**>(d, deque::kFinish + deque::kNode)[-1] + 0x78;
        Step* const last = reinterpret_cast<Step*>(back - 0xC);
        step.occupant = nullptr;
        step.last = 0;
        if (last->last)
            return ok;
        ok = 0;
        if (!last->occupant)
            continue;
        if (Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x40u))
            ok = Virtual<std::uint8_t>(self, slot::kExtendOnRoute, last, &step);
        else
            ok = Virtual<std::uint8_t>(self, slot::kExtendFree, last, &step);
        if (!ok) {
            if (void* const object = step.occupant) {
                step.occupant = nullptr;
                Release(object);
            }
            last->last = 1;
            return 0;
        }
        char* const current = At<char*>(d, deque::kFinish + deque::kCurrent);
        if (current == At<char*>(d, deque::kFinish + deque::kLast) - 0xC) {
            CallThis<void>(addr::SC4AutomatonDeque204_PushBackSlow, d, &step);
        } else {
            if (current) {
                At<void*>(current, 0) = step.occupant;
                if (step.occupant)
                    AddRef(step.occupant);
                At<std::uint32_t>(current, 4) = step.key;
                At<std::uint8_t>(current, 8) = step.last;
            }
            At<char*>(d, deque::kFinish + deque::kCurrent) += 0xC;
        }
        if (step.occupant)
            Release(step.occupant);
    }
    return ok;
}

// 0x00550F40, slot 167 (step, out step), ret 8: a step on from a free
// automaton's. The traffic network map's neighbour of the step's occupant
// (slot 15 with the key, 0 and the kind; false without one) into the out
// step's occupant (referenced; false when none); the side order from
// 0x0054E6F0 (the occupant, the neighbour, the opposite side, an out table
// of four bytes; it answers how many to try). For each of the four sides,
// unless slot 141 is on and the side is the key's entry: the occupant's
// slot 53 (kind, opposite, side) sets the out key and answers true; else,
// among the sides to try and when the automaton is not held (0x0054EA30),
// the network part's slot 9 (kind, opposite, 0xFF, the generator, the out
// key, 0) and the out step marked last; true. False after four. AL only.
std::uint8_t __fastcall ExtendFree(char* self, void*, Step* from, Step* out)
{
    static bool logged;
    FirstCall("ExtendFree", logged);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    const std::uint32_t key = from->key;
    const std::uint32_t kind = (key >> 24) & 0xFu;
    const std::uint32_t entry = (key >> 8) & 0xFFu;
    void** const next = Virtual<void**>(map, map::kNeighbour, from->occupant, key, 0u, kind);
    if (!next)
        return 0;
    void* const occupant = *next;
    AssignReference(&out->occupant, occupant);
    if (!out->occupant)
        return 0;
    const std::uint32_t opposite = ((key & 0xFFu) + 2) & 3u;
    // The opposite side is the low byte of a word holding the neighbour's
    // address; the side the low byte of one holding the automaton's.
    const std::uint32_t across =
        (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)) & 0xFFFFFF00u) | opposite;
    void* const network = Virtual<void*>(out->occupant, kNetworkPartSlot);
    std::uint8_t sides[4] = {}; // stack garbage in the game; the map's slot 6 fills it
    const std::uint8_t tries = static_cast<std::uint8_t>(CallThis<std::uint32_t>(
        addr::cSC4AutomatonBase_ChooseSide, self, out->occupant, static_cast<void*>(next), across,
        static_cast<std::uint8_t*>(sides)));
    const std::uint32_t upper = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self)) & 0xFFFFFF00u;
    for (std::uint8_t i = 0; i < 4; ++i) {
        const std::uint8_t side = sides[i];
        if (Virtual<std::uint8_t>(self, slot::kSlot141) && side == entry)
            continue;
        if (Virtual<std::uint8_t>(out->occupant, kNeighbourSlot, kind, across, upper | side)) {
            out->key = (((kind << 16) | opposite) << 8) | side;
            return 1;
        }
        if (i < tries && !CallThis<std::uint8_t>(addr::cSC4AutomatonBase_IsHeld, self)) {
            Virtual<char**>(network, part::kFindFrom, kind, across, 0xFFu,
                            static_cast<std::uint32_t>(addr::cSC4AutomatonCore_Random), &out->key, 0u);
            out->last = 1;
            return 1;
        }
    }
    return 0;
}

// 0x005527E0, slot 168 (step, out step), ret 8: a step on along the route.
// False with an empty route. The route's first cell's list on the traffic
// network map (slot 14) and the cell popped (inline, as 0x00551F70). With
// the route now empty: the out step marked last, the map's neighbour of the
// step's occupant (slot 15 with the key, 1 and the kind; false without one)
// into the out step and, from the key's side, the first of the four sides
// its slot 53 (kind, opposite, side) answers for sets the out key; false
// after four. Else the side towards the next cell (1 at a lower z, 3 at a
// higher, else 0 at a lower x, else 2) and the first occupant of the cell's
// list whose slot 53 (kind, opposite, side) answers: the out step's
// occupant, its key, not last; true. False when none does. AL only.
std::uint8_t __fastcall ExtendOnRoute(char* self, void*, Step* from, Step* out)
{
    static bool logged;
    FirstCall("ExtendOnRoute", logged);
    void* const map = Virtual<void*>(City(), city::kTrafficNetwork);
    const std::uint32_t key = from->key;
    const std::uint32_t kind = (key >> 24) & 0xFu;
    // A byte with three bytes of the game's stack above it.
    const std::uint32_t opposite = ((key & 0xFFu) + 2) & 3u;
    char* const route = self + field::kRoute;
    if (DequeEmpty(route))
        return 0;
    const std::uint8_t* const cell = At<std::uint8_t*>(route, deque::kStart + deque::kCurrent);
    const std::int32_t x = cell[0], z = cell[1];
    char* node = Virtual<char*>(map, map::kCellList, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
    char*& current = At<char*>(route, deque::kStart + deque::kCurrent);
    if (current != At<char*>(route, deque::kStart + deque::kLast) - 3) {
        current += 3;
    } else {
        char* const first = At<char*>(route, deque::kStart + deque::kFirst);
        if (first)
            CallCdecl<void>(addr::SC4_Free, first);
        char** const at = ++At<char**>(route, deque::kStart + deque::kNode);
        At<char*>(route, deque::kStart + deque::kFirst) = *at;
        At<char*>(route, deque::kStart + deque::kLast) = *at + 0x7E;
        current = At<char*>(route, deque::kStart + deque::kFirst);
    }
    // The side words are the low byte of the step argument's stack slot:
    // their upper three bytes are the step pointer's.
    const std::uint32_t upper = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(from)) & 0xFFFFFF00u;
    if (DequeEmpty(route)) {
        out->last = 1;
        void** const next = Virtual<void**>(map, map::kNeighbour, from->occupant, key, 1u, kind);
        if (!next)
            return 0;
        CallThis<void>(addr::SC4RefPtr_Assign, &out->occupant, *next);
        std::uint32_t side = key & 3u;
        for (int i = 0; i < 4; ++i) {
            if (Virtual<std::uint8_t>(out->occupant, kNeighbourSlot, kind, opposite, upper | side)) {
                out->key = (((kind << 16) | opposite) << 8) | side;
                return 1;
            }
            side = (side + 1) & 3u;
        }
        return 0;
    }
    const std::uint8_t* const ahead = At<std::uint8_t*>(route, deque::kStart + deque::kCurrent);
    std::uint32_t side;
    const std::int32_t dz = static_cast<std::int32_t>(ahead[1]) - z;
    if (dz < 0)
        side = 1;
    else if (dz > 0)
        side = 3;
    else
        side = static_cast<std::int32_t>(ahead[0]) - x < 0 ? 0u : 2u;
    for (; node; node = At<char*>(node, 0x54)) {
        void* const occupant = At<void*>(node, 0);
        if (!occupant || !Virtual<std::uint8_t>(occupant, kNeighbourSlot, kind, opposite, upper | side))
            continue;
        CallThis<void>(addr::SC4RefPtr_Assign, &out->occupant, occupant);
        out->key = (((kind << 16) | opposite) << 8) | side;
        out->last = 0;
        return 1;
    }
    return 0;
}

// 0x00552520, slot 169 (step iterator, point, out key, out distance), ret
// 0x1C: the best key to take on the step's occupant. Its network part's keys
// from the point (slot 6 with an out vector, the kind, the point, the key's
// entry and side bytes and 32 with the automata flag 0x40, else 4); none,
// false. One key: it, and the distance from the point to its path's first
// point; true. More: for each key its path's first point's distance (as a
// float) plus, unless the step is the deque's last, what slot 169 answers
// for the next step from the path's last point (a key whose next step has
// none is skipped); plus 0x00B43D54's slot 27 (occupant, key) x 0x00B21D2C;
// the least not below 0 (from FLT_MAX) taken. The vector freed (SC4::Free).
// AL only.
std::uint8_t __fastcall BestKey(char* self, void*, DequeIterator it, const float* point, std::uint32_t* outKey,
                                float* outDistance)
{
    static bool logged;
    FirstCall("BestKey", logged);
    Step* const step = reinterpret_cast<Step*>(it.current);
    const std::uint32_t kind = (step->key >> 24) & 0xFu;
    // Bytes with three bytes of the game's stack above them.
    const std::uint32_t side = step->key & 0xFFu;
    const std::uint32_t entry = (step->key >> 8) & 0xFFu;
    std::uint8_t answer = 0;
    void* const network = Virtual<void*>(step->occupant, kNetworkPartSlot);
    const float reach = Virtual<std::uint8_t>(self, slot::kCheckFlag, 0x40u) ? 32.0f : 4.0f;
    std::uint32_t* keys[3] = {};
    Virtual<std::uint32_t>(network, part::kKeys, static_cast<void*>(keys), kind, point, entry, side, reach);
    std::uint32_t* const begin = keys[0];
    std::uint32_t* const end = keys[1];
    if (begin == end) {
        if (begin)
            CallCdecl<void>(addr::SC4_Free, begin);
        return 0;
    }
    if ((Difference(end, begin) & ~3) == 4) {
        const std::uint32_t only = *begin;
        *outKey = only;
        char** const path = Virtual<char**>(network, part::kPath, only, 0u);
        const float* const p = reinterpret_cast<const float*>(path[0]);
        const double dx = static_cast<double>(p[0]) - point[0];
        const double dy = static_cast<double>(p[1]) - point[1];
        const double dz = static_cast<double>(p[2]) - point[2];
        *outDistance = static_cast<float>(std::sqrt((dy * dy + dz * dz) + dx * dx));
        CallCdecl<void>(addr::SC4_Free, begin);
        return 1;
    }
    At<std::uint32_t>(outDistance, 0) = 0x7F7FFFFFu;
    *outKey = 0;
    for (std::uint32_t* k = begin; k != end; ++k) {
        char* const finish = At<char*>(self, field::kDeque204 + deque::kFinish + deque::kCurrent);
        const std::uint32_t candidate = *k;
        char** const path = Virtual<char**>(network, part::kPath, candidate, 0u);
        const float* const p = reinterpret_cast<const float*>(path[0]);
        const double dz = static_cast<double>(p[2]) - point[2];
        const double dy = static_cast<double>(p[1]) - point[1];
        const double dx = static_cast<double>(p[0]) - point[0];
        float distance = static_cast<float>(std::sqrt((dx * dx + dy * dy) + dz * dz));
        if (it.current != finish) {
            DequeIterator next = {it.current + 0xC, it.first, it.last, it.node};
            if (next.current == it.last) {
                next.first = it.node[1];
                next.node = it.node + 1;
                next.last = next.first + 0x78;
                next.current = next.first;
            }
            if (next.current != At<char*>(self, field::kDeque204 + deque::kFinish + deque::kCurrent)) {
                std::uint32_t nextKey = 0; // stack garbage in the game; slot 169 sets it when true
                float more = 0.0f;
                if (!Virtual<std::uint8_t>(self, slot::kBestKey, next,
                                           reinterpret_cast<const float*>(path[1]) - 3, &nextKey, &more))
                    continue;
                distance = static_cast<float>(static_cast<double>(more) + distance);
            }
        }
        const std::uint16_t cost = static_cast<std::uint16_t>(
            Virtual<std::uint32_t>(Global<void*>(addr::SC4_Service43D54), kServiceCount, step->occupant, candidate));
        const double total = static_cast<double>(cost) * Global<float>(addr::SC4AutomataTuning_KeyCost21D2C) + distance;
        if (!(total >= Zero()))
            continue;
        if (total < *outDistance) {
            *outDistance = static_cast<float>(total);
            *outKey = candidate;
            answer = 1;
        }
    }
    if (begin)
        CallCdecl<void>(addr::SC4_Free, begin);
    return answer;
}

// 0x0054D860, slot 173 (occupant, key), ret 8: the occupant's network
// part's path from the key (slot 9 with its kind (bits 24..27), the key
// shifted down 8, 0xFF, the generator, the occupant argument's own slot as
// the out word, 0) and slot 132 (occupant, the out word, path, true). False
// without an occupant, a part or a path. AL only.
std::uint8_t __fastcall EnterFrom(char* self, void*, void* occupant, std::uint32_t key)
{
    static bool logged;
    FirstCall("EnterFrom", logged);
    if (!occupant)
        return 0;
    void* const network = Virtual<void*>(occupant, kNetworkPartSlot);
    if (!network)
        return 0;
    std::uint32_t found = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(occupant));
    char** const path = Virtual<char**>(network, part::kFindFrom, (key >> 24) & 0xFu, key >> 8, 0xFFu,
                                        static_cast<std::uint32_t>(addr::cSC4AutomatonCore_Random), &found, 0u);
    if (!path)
        return 0;
    Virtual<std::uint32_t>(self, slot::kSlot132, occupant, found, path, 1u);
    return 1;
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
            log::Write("automata vehicle %s: no address entry for 0x%08X", what,
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
    log::Write("automata vehicle %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4Vehicle_Create, VehicleCreate),
    OPENSC4_DETOUR(addr::cSC4Vehicle_PoolAllocate, PoolAllocate),
    OPENSC4_DETOUR(addr::cSC4Vehicle_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Construct, BaseConstruct),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Construct, VehicleConstruct),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Destroy, BaseDestroy),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Destroy, VehicleDestroy),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_DeletingDestructor, BaseDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4Vehicle_DeletingDestructor, VehicleDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_DestroyDeque204, DestroyDeque204),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ReleaseRange204, ReleaseRange204),
    OPENSC4_DETOUR(addr::cSC4Vehicle_DestroyDeque27C, DestroyDeque27C),
    OPENSC4_DETOUR(addr::cSC4Vehicle_ReleaseRange27C, ReleaseRange27C),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_QueryInterface, BaseQueryInterface),
    OPENSC4_DETOUR(addr::cSC4Vehicle_QueryInterface, VehicleQueryInterface),
    OPENSC4_DETOUR(addr::cSC4Vehicle_GetGZCLSID, VehicleGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_GetFloat200, GetFloat200),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_GetRoute, GetRoute),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_HasFlag8, HasFlag8),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_GetByte1E8, GetByte1E8),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_GetDeque204, GetDeque204),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_GetStaticByte, GetStaticByte),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_GetPositionAddress, GetPositionAddress),
    OPENSC4_DETOUR(addr::cSC4Vehicle_GetPoint264, GetPoint264),
    OPENSC4_DETOUR(addr::cSC4Vehicle_GetPoint270, GetPoint270),
    OPENSC4_DETOUR(addr::cSC4Vehicle_GetDeque27C, GetDeque27C),
    OPENSC4_DETOUR(addr::cSC4Vehicle_GetManager, GetManager),
    OPENSC4_DETOUR(addr::cSC4Vehicle_DequeBack27C, DequeBack27C),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_IsNearPoint, IsNearPoint),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_DirectionTo, DirectionTo),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_IsHeld, IsHeld),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_BestDirection, BestDirection),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_NetworkTest, NetworkTest),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_NearestAutomaton, NearestAutomaton),
    OPENSC4_DETOUR(addr::cSC4Vehicle_NearestVehicle, NearestVehicle),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_NearestOnNetwork, NearestOnNetwork),
};

// Part vb2.
const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Init, BaseInit),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Init, VehicleInit),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Shutdown, BaseShutdown),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Shutdown, VehicleShutdown),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ReleaseDeque204, ReleaseDeque204),
    OPENSC4_DETOUR(addr::SC4AutomatonDeque204_Clear, ClearDeque204),
    OPENSC4_DETOUR(addr::SC4VehicleDeque27C_Clear, ClearDeque27C),
    OPENSC4_DETOUR(addr::cSC4Vehicle_ReleaseModels, ReleaseModels),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Delete, VehicleDelete),
    OPENSC4_DETOUR(addr::SC4AutomatonDeque204_PushBackSlow, PushBackSlow204),
    OPENSC4_DETOUR(addr::SC4AutomatonDeque204_PushBack, PushBack204),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_PushRecordListBack, PushRecordListBack),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Write, BaseWrite),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Read, BaseRead),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Write, VehicleWrite),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Read, VehicleRead),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_SetByte1E8, SetByte1E8),
    OPENSC4_DETOUR(addr::cSC4Vehicle_SetPoint264, SetPoint264),
    OPENSC4_DETOUR(addr::cSC4Vehicle_SetPoint270, SetPoint270),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ClearFloat200, ClearFloat200),
    OPENSC4_DETOUR(addr::cSC4Vehicle_DrawFloat200, DrawFloat200),
    OPENSC4_DETOUR(addr::cSC4Vehicle_DrawByte1E8, DrawByte1E8),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Stop, Stop),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Go, Go),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_SetFlag8, SetFlag8),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_RemoveNow, RemoveNow),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Slot172, Slot172),
    OPENSC4_DETOUR(addr::cSC4Vehicle_Forget, Forget),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_DebugText, BaseDebugText),
    OPENSC4_DETOUR(addr::cSC4Vehicle_DebugText, VehicleDebugText),
};

// Part vb3.
const Detour kMove[] = {
    OPENSC4_DETOUR(addr::SC4RouteDeque_PopFront, RoutePopFront),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_SetDestination, SetDestination),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_WeighSides, WeighSides),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ChooseSide, ChooseSide),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_PathTo, PathTo),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_EnterOccupant, EnterOccupant),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_AdvanceOnPath, AdvanceOnPath),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_OffsetSideways, OffsetSideways),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ClearAhead, ClearAhead),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_MoveOnPath, MoveOnPath),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_MoveToDestination, MoveToDestination),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_PlaceOnNetwork, PlaceOnNetwork),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_NextRouteCell, NextRouteCell),
};

// Part vb4.
const Detour kStates[] = {
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_Frame, Frame),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_StateFrame, StateFrame),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_EnterState, EnterState),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_InitEntry, InitEntry),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_LeaveState, LeaveState),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_EraseRecord, EraseRecord),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_ClearRecord, ClearRecord),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_SetRoute, SetRoute),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_RemoveSame, RemoveSame),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_FindAhead, FindAhead),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_CheckAhead, CheckAhead),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_QueueFits, QueueFits),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_WaitFrame, WaitFrame),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_DriveFrame, DriveFrame),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_NextStep, NextStep),
    OPENSC4_DETOUR(addr::SC4AutomatonDeque204_PopFront, StepPopFront),
    OPENSC4_DETOUR(addr::SC4AutomatonDeque204_Increment, StepIncrement),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_FillAhead, FillAhead),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ExtendFree, ExtendFree),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_ExtendOnRoute, ExtendOnRoute),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_BestKey, BestKey),
    OPENSC4_DETOUR(addr::cSC4AutomatonBase_EnterFrom, EnterFrom),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kLife, sizeof kLife / sizeof kLife[0], "life") &&
           InstallList(kMove, sizeof kMove / sizeof kMove[0], "move") &&
           InstallList(kStates, sizeof kStates / sizeof kStates[0], "states");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.vehicle", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Vehicle_Construct,
                        "Our automaton base and cSC4Vehicle, the object and its queries: the vehicle's factory and "
                        "pool, both constructors, both destructor bodies with their deques, both deleting "
                        "destructors, QueryInterface of both and the vehicle's GetGZCLSID; the accessors, the near "
                        "and direction tests, the held test, the best direction, the network test and the nearest "
                        "automaton searches (36 detours, part vb1); Init and Shutdown of both with the deques' "
                        "release and the models', Write and Read of both, the +0x204 deque's push and the record "
                        "list's, the setters and the debug texts (30 detours, part vb2); the movement on frames: "
                        "SetDestination, the moves along a path and straight, the path's walk and sideways offset, "
                        "entering an occupant, placing on the network, the next route cell, the room ahead and the "
                        "side weights (13 detours, part vb3); the states on frames: the base's frame, the state "
                        "frames, entering and leaving states, the records, the route, the same-type removal, the "
                        "automaton ahead and the wait, the queue test and the steps ahead (22 detours, part vb4; "
                        "src/core/automata_vehicle.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_vehicle
