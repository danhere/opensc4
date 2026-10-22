// Our automaton base and cSC4Vehicle, part vb1 (build 641): the object (the
// vehicle's factory and pool, the two constructors, the two destructor
// bodies with their deques, the two deleting destructors, QueryInterface of
// both and the vehicle's GetGZCLSID) and the queries that do not run on
// frames by themselves (the accessors, the near and direction tests, the
// held test, the best direction, the network test and the three nearest
// automaton searches). The class map is in src/core/automata_vehicle.h and
// the survey in docs/automata.md, "Part vb". Calls between functions of the
// part go to the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Group, off by default:
//   simulators.automata.vehicle: the 36 detours listed in kObject below.
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

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.vehicle", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Vehicle_Construct,
                        "Our automaton base and cSC4Vehicle, the object and its queries: the vehicle's factory and "
                        "pool, both constructors, both destructor bodies with their deques, both deleting "
                        "destructors, QueryInterface of both and the vehicle's GetGZCLSID; the accessors, the near "
                        "and direction tests, the held test, the best direction, the network test and the nearest "
                        "automaton searches (36 detours; part vb1, src/core/automata_vehicle.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_vehicle
