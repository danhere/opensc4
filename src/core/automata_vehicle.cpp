// Our automaton base and cSC4Vehicle, parts vb1 and vb2 (build 641). vb1:
// the object (the vehicle's factory and pool, the two constructors, the two
// destructor bodies with their deques, the two deleting destructors,
// QueryInterface of both and the vehicle's GetGZCLSID) and the queries that
// do not run on frames by themselves (the accessors, the near and direction
// tests, the held test, the best direction, the network test and the three
// nearest automaton searches). vb2: Init and Shutdown of both (with the
// deques' release and emptying and the vehicle's models), Write and Read of
// both, the +0x204 deque's push, the managers' record list push, the
// derived classes' operator delete, the setters and the debug texts. The
// class map is in src/core/automata_vehicle.h and the survey in
// docs/automata.md, "Part vb". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Group, off by default:
//   simulators.automata.vehicle: the 36 detours in kObject (vb1) and the 30
//   in kLife (vb2) below.
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

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kLife, sizeof kLife / sizeof kLife[0], "life");
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
                        "list's, the setters and the debug texts (30 detours, part vb2; "
                        "src/core/automata_vehicle.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_vehicle
