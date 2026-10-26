// Our trains, part tr1 (build 641): cSC4TrainCar on cSC4Vehicle
// (src/core/automata_vehicle.cpp), cSC4Train on the car and
// cSC4MassTransitTrain on the train. tr1: the objects (the three factories
// and constructors, the car's pool, the car's and the train's deleting
// destructors, the train's destructor body, the two QueryInterfaces and the
// three GetGZCLSIDs), the save (the train's and the mass transit train's
// Writes and Reads; the car's are the bus's, part ro1), the life cycle (the
// car's and the train's Init, the train's Shutdown) and the queries (the
// car's slots 50, 65 and 120, the train's 175 and 193 to 196). The layout is
// in src/core/automata_trains.h and the survey in docs/automata.md, "Part
// tr". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.trains: the 31 detours in kObject (tr1), the 13 in
//   kFrame (tr2a), the 5 in kMove (tr2b) and the 1 in kTrainFrame (tr3a)
//   below.
//
// What it does. The car is 0x2C8 bytes from its own pool (0x00B0A09C, as
// the vehicle pool: other sizes go to SC4::operator_new) and goes back there
// through its deleting destructor; the train and the mass transit train are
// 0x2FC bytes from SC4::operator_new and go back through
// SC4::operator_delete. Each constructor runs the one before with four words
// (the factories pass zeros) and sets its six vtables. The car clears its
// byte, sets its three floats to 0x00B22964 and draws a time as
// cSC4Vehicle's constructor does, from its own range (0x00B21D0C to
// 0x00B21D10); the train zeroes its words and its vector and sets its point
// to (-1, -1, -1).
//
// Oddities kept: the car's slot 65 hands the call to its train (slot 50)
// unless its state is 0x14, and slot 50 answers the car itself when it has
// no train at +0xB0, so a car with neither recurses (the game's own cars
// always have one); the car's slot 120 does not test the U-Drive controls
// for null while a disaster is active; the train's Init leaves the car
// group as it was when the train is in none of the five groups, and draws
// the wanted count only for a group set; the train's +0x2EC is not set by
// its constructor; the train's QueryInterface answers the car's class ID
// too, and the mass transit train has the train's (it answers the train's
// and the car's, not its own); the train's Read reads its first version word
// into an unset local (ours zero) and its second into the stream argument's
// own stack slot, as the mass transit train's does its one; its count and
// each car reference go to locals as the trailer's do (the car local zeroed
// before each reference, the cars not emptied first); its Shutdown stops
// and releases the cars, and its destructor frees the cars' buffer without
// releasing them. Slot 195 passes its whole argument on to slot 193 but stores its low byte; slot
// 193 hands a count above the car count back to slot 195. AL only in the
// game (ours zero-extends): the car's and the train's Inits, the train's
// Shutdown, Write and Read, the mass transit train's Read when the version
// is wrong (the stream's vtable with AL 0), slots 65 and 196.
//
// Floats: the car's time is cRZRandomSuper::Between's word as a signed int
// times the float 0x00A9C7C0; the train's Init draws another with
// cRZRandom::Range between the two words read as unsigned times that float
// (doubles, as the x87 at 53 bits).
//
// Still the game's, at their addresses: cSC4Vehicle's code (part vb), the
// bus's Write and Read the car's +8 vtable holds (part ro1), the jumps the
// vtables hold (0x0053C950 and 0x0053C960, the mass transit train's Init and
// Shutdown; 0x0053CA10; 0x00557070; 0x00555A50 shared with the trailer;
// 0x0055B740), the adjustor thunks, the folds (0x00441A50, 0x00648F00,
// 0x004FC900, 0x005DEB40), the stream helpers, SC4PtrVector::InsertN, the
// pool's page code and the generator.

#include "core/automata_trains.h"

#include "core/address_check.h"
#include "core/fixed_seed.h"
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
#include <initializer_list>

namespace opensc4::automata_trains {
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
    log::Write("automata trains: first %s", name);
}

constexpr std::size_t kAddRef = 0x04, kRelease = 0x08;
constexpr std::size_t kVtable4 = 0x04, kSerializable = 0x08;
constexpr std::uint32_t kOccupantIid = 0xA821EF94; // the train's occupant is read back as this
constexpr std::uint32_t kNetworkIid = 0x4A10AFC2; // the car's slot 132 asks its occupant for it

void Release(void* object) { Virtual<std::uint32_t>(object, kRelease); }

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

// The version word of a Read that reads it into the stream argument's own
// stack slot: what is there when the read fails is the address's low word.
std::uint16_t VersionInArgument(void* s)
{
    std::uint16_t version = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(s));
    StreamCall(s, stream::kUint16, &version);
    return version;
}

// A vector of words (begin, end, capacity): push_back as the game inlines it
// (SC4PtrVector::InsertN when full; a null end is not written through).
void PushWord(char* vector, std::uint32_t* value)
{
    std::uint32_t* const end = At<std::uint32_t*>(vector, 4);
    if (end != At<std::uint32_t*>(vector, 8)) {
        if (end)
            *end = *value;
        At<std::uint32_t*>(vector, 4) = end + 1;
        return;
    }
    char allocator = 0; // the game passes the address of a slot of its stack
    CallThis<void>(addr::SC4PtrVector_InsertN, vector, end, value, static_cast<void*>(&allocator), 1u, 1u);
}

std::uint32_t CarCount(char* self)
{
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(At<char*>(self, train::kCars + 4) - At<char*>(self, train::kCars)) >> 2);
}

// The smoothed value at +0x1A0 set to `value` when it differs from its
// target (or is unordered), as cSC4Vehicle's constructor does: its value
// now (0x004FA960), the target, duration and elapsed time zero.
void SetSmoothed(char* self, float value)
{
    char* const smoothed = self + field::kSmoothed;
    if (!(value == At<float>(smoothed, 4))) {
        const double now = CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, smoothed);
        At<float>(smoothed, 0) = static_cast<float>(now);
        At<float>(smoothed, 4) = value;
        At<std::uint32_t>(smoothed, 8) = 0;
        At<std::uint32_t>(smoothed, 0xC) = 0;
    }
}

// ====================================================================================
// The objects

constexpr std::uintptr_t kCarTables[6] = {addr::cSC4TrainCar_Vtable,   addr::cSC4TrainCar_Vtable4,
                                          addr::cSC4TrainCar_Vtable8,  addr::cSC4TrainCar_VtableC,
                                          addr::cSC4TrainCar_Vtable20, addr::cSC4TrainCar_Vtable38};
constexpr std::uintptr_t kTrainTables[6] = {addr::cSC4Train_Vtable,   addr::cSC4Train_Vtable4,
                                            addr::cSC4Train_Vtable8,  addr::cSC4Train_VtableC,
                                            addr::cSC4Train_Vtable20, addr::cSC4Train_Vtable38};
constexpr std::uintptr_t kMassTransitTables[6] = {
    addr::cSC4MassTransitTrain_Vtable,   addr::cSC4MassTransitTrain_Vtable4,  addr::cSC4MassTransitTrain_Vtable8,
    addr::cSC4MassTransitTrain_VtableC,  addr::cSC4MassTransitTrain_Vtable20, addr::cSC4MassTransitTrain_Vtable38};

// 0x00557290, cdecl (size): a 0x2C8-byte block from the car pool
// (0x00B0A09C, its page at +4: the free head at +8, the count at +0xC), a
// new page from SC4EdgePool::Grow when the page has none; other sizes from
// SC4::operator_new.
void* __cdecl CarPoolAllocate(std::size_t size)
{
    static bool logged;
    FirstCall("CarPoolAllocate", logged);
    if (size != kCarSize)
        return CallCdecl<void*>(addr::SC4_operator_new, size);
    char* block = At<char*>(Global<char*>(addr::cSC4TrainCar_Pool + 4), 8);
    if (!block)
        block = CallThis<char*>(addr::SC4EdgePool_Grow, reinterpret_cast<void*>(addr::cSC4TrainCar_Pool));
    At<char*>(Global<char*>(addr::cSC4TrainCar_Pool + 4), 8) = At<char*>(block, 0);
    ++At<std::uint32_t>(Global<char*>(addr::cSC4TrainCar_Pool + 4), 0xC);
    return block;
}

// 0x005579E0, on the car pool (block), ret 4: the block back onto its page's
// free list (the page is the block's address & 0xFFFF0000); the page let go
// at count 0 (SC4Pool::ReleaseBlock) and reopened at 0x40000045
// (SC4Pool::ReopenBlock).
void __fastcall CarPoolFree(void* pool, void*, char* block)
{
    static bool logged;
    FirstCall("CarPoolFree", logged);
    if (!block)
        return;
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(block) & 0xFFFF0000u);
    At<char*>(block, 0) = At<char*>(page, 8);
    At<char*>(page, 8) = block;
    const std::uint32_t count = --At<std::uint32_t>(page, 0xC);
    if (count == 0)
        CallThis<void>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (count == 0x40000045u)
        CallThis<void>(addr::SC4Pool_ReopenBlock, pool, page);
}

// 0x0055B150, the car's constructor (four words for cSC4Vehicle's), ret
// 0x10: the byte +0x2B8 zero, the floats +0x2BC..+0x2C4 0x00B22964's; a
// time drawn (cRZRandomSuper::Between of 0x00B21D0C and 0x00B21D10 on the
// automata's generator, as a signed int, times 0x00A9C7C0) into +0x80 and,
// when it differs, into the smoothed value.
char* __fastcall CarConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                              std::uint32_t d)
{
    static bool logged;
    FirstCall("CarConstruct", logged);
    CallThis<char*>(addr::cSC4Vehicle_Construct, self, a, b, c, d);
    SetVtables(self, kCarTables);
    At<std::uint8_t>(self, car::kGroupByte) = 0;
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, car::kFloats + i) = Global<std::uint32_t>(addr::cSC4TrainCar_DefaultFloat);
    const std::uint32_t drawn =
        CallThis<std::uint32_t>(addr::cRZRandomSuper_Between, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random),
                                Global<std::uint32_t>(addr::cSC4TrainCar_DrawLow),
                                Global<std::uint32_t>(addr::cSC4TrainCar_DrawHigh));
    // FILD of the answer as a signed int, times the float.
    const float value = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(drawn)) *
                                           static_cast<double>(Global<float>(addr::cSC4TrainCar_DrawScale)));
    At<float>(self, field::kDraw) = value;
    SetSmoothed(self, value);
    return self;
}

// 0x0055B2F0, the train's constructor (four words for the car's), ret 0x10:
// the wanted count, the car group, the vector, +0x2E8 and +0x2F0 zero, the
// point -1, -1, -1.
char* __fastcall TrainConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                std::uint32_t d)
{
    static bool logged;
    FirstCall("TrainConstruct", logged);
    CallThis<char*>(addr::cSC4TrainCar_Construct, self, a, b, c, d);
    At<std::uint32_t>(self, train::kWanted) = 0;
    At<std::uint32_t>(self, train::kCarGroup) = 0;
    SetVtables(self, kTrainTables);
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, train::kCars + i) = 0;
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, train::kPoint + i) = 0xBF800000u;
    At<std::uint32_t>(self, train::kOccupant) = 0;
    At<std::uint32_t>(self, train::kWord2F0) = 0;
    return self;
}

// 0x0053CBB0, the mass transit train's constructor: the train's with four
// zero words, its six vtables.
char* __fastcall MassTransitConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("MassTransitConstruct", logged);
    CallThis<char*>(addr::cSC4Train_Construct, self, 0u, 0u, 0u, 0u);
    SetVtables(self, kMassTransitTables);
    return self;
}

// 0x00514910, the car's GZCOM factory (cdecl): a 0x2C8-byte block from the
// car pool and the constructor with four zero words; null when the block is.
void* __cdecl CarCreate()
{
    static bool logged;
    FirstCall("CarCreate", logged);
    void* const block = CallCdecl<void*>(addr::cSC4TrainCar_PoolAllocate, kCarSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4TrainCar_Construct, block, 0u, 0u, 0u, 0u);
}

// 0x00514940, the train's GZCOM factory (cdecl): 0x2FC bytes from
// SC4::operator_new and the constructor with four zero words.
void* __cdecl TrainCreate()
{
    static bool logged;
    FirstCall("TrainCreate", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, kTrainSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4Train_Construct, block, 0u, 0u, 0u, 0u);
}

// 0x00514760, the mass transit train's (cdecl): 0x2FC bytes from
// SC4::operator_new, the constructor (a tail jump).
void* __cdecl MassTransitCreate()
{
    static bool logged;
    FirstCall("MassTransitCreate", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, kTrainSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4MassTransitTrain_Construct, block);
}

// 0x0055B2C0, the car's deleting destructor (flags), ret 4: cSC4Vehicle's
// body, then with flag 1 and a non-null this the block back to the car pool.
char* __fastcall CarDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("CarDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
    if ((flags & 1) && self)
        CallThis<void>(addr::cSC4TrainCar_PoolFree, reinterpret_cast<void*>(addr::cSC4TrainCar_Pool), self);
    return self;
}

// 0x0053CB80, the train's destructor body: the occupant released (not
// cleared), the cars' buffer freed (SC4::Free; the cars not released), then
// cSC4Vehicle's (a tail jump).
void* __fastcall TrainDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("TrainDestroy", logged);
    if (void* const occupant = At<void*>(self, train::kOccupant))
        Release(occupant);
    if (void* const cars = At<void*>(self, train::kCars))
        CallCdecl<void>(addr::SC4_Free, cars);
    return CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
}

// 0x0053CCE0, the train's and the mass transit train's deleting destructor
// (flags), ret 4: SC4::operator_delete, no null test.
char* __fastcall TrainDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("TrainDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4Train_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The QueryInterfaces (iid, out), ret 8: the class IDs given answer the
// object (AddRef, AL 1); the rest cSC4Vehicle's (a tail jump).
std::uint32_t QueryAs(char* self, std::uint32_t iid, void** out, bool train)
{
    if (iid != kCarClassId && !(train && iid == kTrainClassId))
        return CallThis<std::uint32_t>(addr::cSC4Vehicle_QueryInterface, self, iid, out);
    *out = self;
    Virtual<std::uint32_t>(self, kAddRef);
    return 1;
}

// 0x00557040, the car's slot 0: its class ID.
std::uint32_t __fastcall CarQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("CarQueryInterface", logged);
    return QueryAs(self, iid, out, false);
}

// 0x00557220, the train's slot 0 (the mass transit train's too): its class
// ID and the car's.
std::uint32_t __fastcall TrainQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("TrainQueryInterface", logged);
    return QueryAs(self, iid, out, true);
}

// The serializable part's slot 5 of each class: its class ID.

// 0x0055B240, the car's.
std::uint32_t __fastcall CarGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("CarGetGZCLSID", logged);
    return kCarClassId;
}

// 0x0055B390, the train's.
std::uint32_t __fastcall TrainGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("TrainGetGZCLSID", logged);
    return kTrainClassId;
}

// 0x0053CC30, the mass transit train's.
std::uint32_t __fastcall MassTransitGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("MassTransitGetGZCLSID", logged);
    return kMassTransitClassId;
}

// ====================================================================================
// The save (this = the object + 8, the serializable part)

// 0x00557930, the train's +8 slot 3 (stream), ret 4: version 2 and the
// car's version 2 (uint16s; the car's Write inlined), cSC4Vehicle's Write;
// when true the cars' count and a reference each (SC4Stream::WriteReference),
// the wanted count, the point (SC4Stream::WriteVector3), the occupant (a
// reference) and +0x2EC. AL cSC4Vehicle's.
std::uint32_t __fastcall TrainWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("TrainWrite", logged);
    char* const self = part - kSerializable;
    StreamCall(s, stream::kUint16, 2u);
    StreamCall(s, stream::kUint16, 2u);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Write, part, s);
    if (!ok)
        return ok;
    char* const cars = self + train::kCars;
    StreamCall(s, stream::kUint32, CarCount(self));
    for (void** car = At<void**>(cars, 0); car != At<void**>(cars, 4); ++car)
        CallCdecl<void>(addr::SC4Stream_WriteReference, s, *car);
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, train::kWanted));
    CallCdecl<void*>(addr::SC4Stream_WriteVector3, s, self + train::kPoint);
    CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(self, train::kOccupant));
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, train::kWord));
    return ok;
}

// 0x0055AAD0, the train's +8 slot 4: AL 0 unless both version words are 2
// (the first into a local, the second into the stream argument's slot);
// cSC4Vehicle's Read; when true the cars (a count, then per car a
// reference read as the car's class into a local zeroed first, pushed when
// not null; the vector not emptied first), the wanted count, the point, the
// occupant (released and cleared, then read as 0xA821EF94) and +0x2EC. AL
// cSC4Vehicle's.
std::uint32_t __fastcall TrainRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("TrainRead", logged);
    char* const self = part - kSerializable;
    std::uint16_t version = 0;
    StreamCall(s, stream::kUint16, &version);
    if (version != 2)
        return 0;
    if (VersionInArgument(s) != 2)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Read, part, s);
    if (!ok)
        return ok;
    std::uint32_t count = 0;
    StreamCall(s, stream::kUint32, &count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t value = 0;
        CallCdecl<void*>(addr::SC4Stream_ReadReference, s, kCarClassId, static_cast<void*>(&value), 0u);
        if (value)
            PushWord(self + train::kCars, &value);
    }
    StreamCall(s, stream::kUint32, &At<std::uint32_t>(self, train::kWanted));
    CallCdecl<void*>(addr::SC4Stream_ReadVector3, s, self + train::kPoint);
    if (void* const occupant = At<void*>(self, train::kOccupant)) {
        At<void*>(self, train::kOccupant) = nullptr;
        Release(occupant);
    }
    CallCdecl<void*>(addr::SC4Stream_ReadReference, s, kOccupantIid, self + train::kOccupant, 0u);
    StreamCall(s, stream::kUint32, &At<std::uint32_t>(self, train::kWord));
    return ok;
}

// 0x0053CB30, the mass transit train's +8 slot 3: version 1, then the
// train's Write (its EAX).
std::uint32_t __fastcall MassTransitWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("MassTransitWrite", logged);
    StreamCall(s, stream::kUint16, 1u);
    return CallThis<std::uint32_t>(addr::cSC4Train_Write, part, s);
}

// 0x0053CB50, the mass transit train's +8 slot 4: AL 0 unless the version
// (into the stream argument's slot) is 1; else the train's Read (its EAX).
std::uint32_t __fastcall MassTransitRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("MassTransitRead", logged);
    if (VersionInArgument(s) != 1)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4Train_Read, part, s);
}

// ====================================================================================
// The life cycle

bool InGroup(char* self, std::uint32_t group)
{
    return Virtual<std::uint8_t>(self + kVtable4, slot::kInGroup, group) != 0;
}

// 0x00556F80, the car's slot 3 (Init): AL 1 at once when initialised (the
// occupant part's slot 5); else cSC4Vehicle's Init, the automata flags
// 0x10000, 0x4000 and 0x10000000 set (slot 30), the byte +0x2B8 whether the
// occupant is in group 0x4217 or 0x4208 (slot 22 of the occupant part). AL
// cSC4Vehicle's.
std::uint32_t __fastcall CarInit(char* self, void*)
{
    static bool logged;
    FirstCall("CarInit", logged);
    if (Virtual<std::uint8_t>(self + kVtable4, slot::kInitialized))
        return 1;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Init, self);
    Virtual<void>(self, slot::kSetFlag, 0x10000u, 1u);
    Virtual<void>(self, slot::kSetFlag, 0x4000u, 1u);
    Virtual<void>(self, slot::kSetFlag, 0x10000000u, 1u);
    At<std::uint8_t>(self, car::kGroupByte) = InGroup(self, 0x4217u) || InGroup(self, 0x4208u) ? 1 : 0;
    return ok;
}

// A word of the tuning read as the x87 reads it unsigned (FILD, plus 2^32
// when negative), times the float 0x00A9C7C0.
double Tuning(std::uintptr_t at)
{
    return static_cast<double>(Global<std::uint32_t>(at)) * static_cast<double>(Global<float>(addr::cSC4TrainCar_DrawScale));
}

// 0x00557A20, the train's slot 3 (the mass transit train's through
// 0x0053C950): AL 1 at once when initialised; else the car's Init, the
// automata flag 0x10000000 cleared; a time drawn (cRZRandom::Range between
// 0x00B21D0C and 0x00B21D10 read as unsigned, times 0x00A9C7C0) into +0x80
// and, when it differs, into the smoothed value. The car group by the
// train's (the first of 0x4216, 0x4215, 0x4217, 0x422C, 0x422E it is in:
// 0x4203, 0x4201, 0x4208, 0x422D, 0x422F; else unchanged); when set, the
// wanted count drawn (SC4Random::Below on the generator: 0x00B21D06 to
// 0x00B21D07 for 0x4203, else 0x00B21D08 to 0x00B21D09, bytes) given to
// slot 195. +0x2F4 and +0x2F8 zero. AL the car's.
std::uint32_t __fastcall TrainInit(char* self, void*)
{
    static bool logged;
    FirstCall("TrainInit", logged);
    if (Virtual<std::uint8_t>(self + kVtable4, slot::kInitialized))
        return 1;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4TrainCar_Init, self);
    Virtual<void>(self, slot::kSetFlag, 0x10000000u, 0u);
    void* const generator = reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random);
    const double high = Tuning(addr::cSC4TrainCar_DrawHigh);
    const double low = Tuning(addr::cSC4TrainCar_DrawLow);
    const float value = static_cast<float>(CallThis<double>(addr::cRZRandom_Range, generator, low, high));
    At<float>(self, field::kDraw) = value;
    SetSmoothed(self, value);
    static constexpr std::uint32_t kGroups[5][2] = {
        {0x4216u, 0x4203u}, {0x4215u, 0x4201u}, {0x4217u, 0x4208u}, {0x422Cu, 0x422Du}, {0x422Eu, 0x422Fu}};
    for (const auto& group : kGroups)
        if (InGroup(self, group[0])) {
            At<std::uint32_t>(self, train::kCarGroup) = group[1];
            break;
        }
    const std::uint32_t carGroup = At<std::uint32_t>(self, train::kCarGroup);
    if (carGroup) {
        const bool first = carGroup == 0x4203u;
        const std::uint32_t least = Global<std::uint8_t>(first ? addr::cSC4Train_CarsLeastA : addr::cSC4Train_CarsLeastB);
        const std::uint32_t most = Global<std::uint8_t>(first ? addr::cSC4Train_CarsMostA : addr::cSC4Train_CarsMostB);
        const std::uint32_t drawn = CallThis<std::uint32_t>(addr::SC4Random_Below, generator, most - least);
        Virtual<void>(self, slot::kSetCarCount, drawn + least);
    }
    At<std::uint32_t>(self, train::kWord2F4) = 0;
    At<std::uint8_t>(self, train::kByte2F8) = 0;
    return ok;
}

// The deques' elements told to the network service as Shutdown walks them:
// for each element (its occupant first) the occupant's cell (slot 59) and
// 0x00B43D54's slot 39 (self, the two words), while the service is set
// (read each step); the walk goes on without it.
void LeaveCells(char* self, char* deque, std::size_t size, std::size_t block)
{
    char* current = At<char*>(deque, 0);
    char* last = At<char*>(deque, 8);
    char** node = At<char**>(deque, 0xC);
    char* const finish = At<char*>(deque, 0x10);
    while (current != finish) {
        if (void* const service = Global<void*>(addr::SC4_Service43D54)) {
            std::int32_t first = 0;
            std::int32_t second = 0;
            Virtual<void>(At<void*>(current, 0), slot::kCell, &first, &second);
            Virtual<void>(service, slot::kLeaveCell, static_cast<void*>(self), first, second);
        }
        current += size;
        if (current == last) {
            ++node;
            current = *node;
            last = current + block;
        }
    }
}

// 0x00559E70, the train's slot 4 (the mass transit train's through
// 0x0053C960): the cells of the base's +0x204 deque's elements and then of
// cSC4Vehicle's +0x27C deque's let go (LeaveCells); cSC4Vehicle's Shutdown;
// the occupant cleared and released; each car (the end re-read each step)
// given slot 13 (0) and released; the cars' vector emptied. AL
// cSC4Vehicle's.
std::uint32_t __fastcall TrainShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("TrainShutdown", logged);
    LeaveCells(self, self + field::kDeque204, 0xC, 0x78);
    LeaveCells(self, self + field::kDeque27C, 8, 0x80);
    const std::uint8_t done = CallThis<std::uint8_t>(addr::cSC4Vehicle_Shutdown, self);
    if (void* const occupant = At<void*>(self, train::kOccupant)) {
        At<void*>(self, train::kOccupant) = nullptr;
        Release(occupant);
    }
    char* const cars = self + train::kCars;
    for (void** car = At<void**>(cars, 0); car != At<void**>(cars, 4); ++car)
        if (void* const object = *car) {
            Virtual<void>(object, slot::kCarStop, 0u);
            Release(object);
        }
    At<void**>(cars, 4) = At<void**>(cars, 0);
    return done;
}

// ====================================================================================
// The queries

// 0x005573F0, the car's slot 50 (its train): the object +0xB0's slot 50 (a
// tail jump) when set, else the car itself.
void* __fastcall CarGetTrain(char* self, void*)
{
    static bool logged;
    FirstCall("CarGetTrain", logged);
    if (void* const object = At<void*>(self, field::kObjectB0))
        return Virtual<void*>(object, slot::kSlot50);
    return self;
}

// 0x00557080, the car's slot 65 (a, b), ret 8: unless the state is 0x14
// (slot 59), the train's (slot 50) slot 65 with both words when there is
// one. AL 1.
std::uint32_t __fastcall CarCollide(char* self, void*, std::uint32_t a, std::uint32_t b)
{
    static bool logged;
    FirstCall("CarCollide", logged);
    if (Virtual<std::uint32_t>(self, slot::kStateType) != 0x14u)
        if (void* const train = Virtual<void*>(self, slot::kSlot50))
            Virtual<std::uint32_t>(train, slot::kCollide, a, b);
    return 1;
}

// 0x005572D0, the car's slot 120 (word), ret 4: while a disaster is active
// (0x00B43D3C's slot 10) the U-Drive controls' (slot 56, no null test)
// float +0x10 raised to 10000 (0x00A949E4) when below it; then the core's
// slot 120 (0x00508D70 through cSC4Vehicle's jump 0x0055B740), its EAX.
std::uint32_t __fastcall CarLightFrame(char* self, void*, std::uint32_t word)
{
    static bool logged;
    FirstCall("CarLightFrame", logged);
    if (Virtual<std::uint8_t>(Global<void*>(addr::SC4_Service43D3C), slot::kDisasterActive)) {
        char* const controls = Virtual<char*>(self, slot::kControls);
        if (At<float>(controls, 0x10) < Global<float>(addr::cSC4TrainCar_DisasterTimer))
            At<std::uint32_t>(controls, 0x10) = 0x461C4000u;
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_LightFrame, self, word);
}

// 0x0053CC20, the train's slot 175: 6.
std::uint32_t __fastcall TrainSlot175(char*, void*)
{
    static bool logged;
    FirstCall("TrainSlot175", logged);
    return 6;
}

// 0x00558D20, the train's slot 193 (count), ret 4: the low byte as the
// count; while there are more cars, the last one (when not null) given slot
// 13 (0) and released and the end moved back; with fewer cars slot 195
// (the whole word).
void __fastcall TrainTrimCars(char* self, void*, std::uint32_t word)
{
    static bool logged;
    FirstCall("TrainTrimCars", logged);
    const std::uint32_t count = word & 0xFF;
    if (count < CarCount(self)) {
        while (CarCount(self) > count) {
            if (void* const last = At<void**>(self, train::kCars + 4)[-1]) {
                Virtual<void>(last, slot::kCarStop, 0u);
                Release(last);
            }
            At<void**>(self, train::kCars + 4) -= 1;
        }
        return;
    }
    if (count > CarCount(self))
        Virtual<void>(self, slot::kSetCarCount, word);
}

// 0x0053CCB0, the train's slot 194: the number of cars.
std::int32_t __fastcall TrainCarCount(char* self, void*)
{
    static bool logged;
    FirstCall("TrainCarCount", logged);
    return static_cast<std::int32_t>(CarCount(self));
}

// 0x005577F0, the train's slot 195 (count), ret 4: slot 193 (the whole
// word) when the low byte is below the number of cars; the low byte into
// the wanted count.
void __fastcall TrainSetCarCount(char* self, void*, std::uint32_t word)
{
    static bool logged;
    FirstCall("TrainSetCarCount", logged);
    const std::uint32_t count = word & 0xFF;
    if (count < CarCount(self))
        Virtual<void>(self, slot::kTrimCars, word);
    At<std::uint32_t>(self, train::kWanted) = count;
}

// 0x0053CC10, the train's slot 196: the wanted count's low byte.
std::uint32_t __fastcall TrainWantedCars(char* self, void*)
{
    static bool logged;
    FirstCall("TrainWantedCars", logged);
    return At<std::uint8_t>(self, train::kWanted);
}

// ====================================================================================
// The frames (tr2a)

float Zero() { return Global<float>(addr::SC4_FloatZero); }
std::uint8_t Bool(std::uint32_t eax) { return static_cast<std::uint8_t>(eax); }

// The template's entry (slot 67): its +0x64, else 0x0046B4A0 on its parent
// +0xC; null without a template or an entry.
char* TemplateEntry(char* self)
{
    char* const owner = Virtual<char*>(self, slot::kTemplate);
    if (!owner)
        return nullptr;
    if (char* const entry = At<char*>(owner, 0x64))
        return entry;
    char* const parent = At<char*>(owner, 0xC);
    return parent ? CallThis<char*>(addr::SC4AutomatonTemplate_FindInherited64, parent) : nullptr;
}

// Each car of the vector (the end re-read each step).
template <typename Each>
void ForEachCar(char* self, Each each)
{
    for (char** car = At<char**>(self, train::kCars); car != At<char**>(self, train::kCars + 4); ++car)
        each(*car);
}

// 0x00557410, the car's slot 110 (state), ret 4: cSC4Vehicle's (0x0055B6D0)
// first. With the state 0x14 and its +0x28 object: that object's slot 13
// (0x10, &the car's slot 5 word, 1); when the train (+0xB0) is in the state
// 0x14 too and its controls (slot 56) have a +0x28, slot 13 again (0x11,
// &the train's slot 5 word, 1) on the car's state's object. Then, when slot
// 192 answers true (a train), slot 65 (0, a point of 0x00B22964's). AL
// cSC4Vehicle's.
std::uint32_t __fastcall CarEnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("CarEnterState", logged);
    const std::uint8_t ok = Bool(CallThis<std::uint32_t>(addr::cSC4Vehicle_EnterState, self, entered));
    if (!entered || At<std::uint32_t>(entered, 4) != 0x14u || !At<void*>(entered, 0x28))
        return ok;
    std::uint32_t id = Virtual<std::uint32_t>(self, slot::kId);
    Virtual<void>(At<void*>(entered, 0x28), slot::kRecordSlot13, 0x10u, &id, 1u);
    // +0xB0 is read again for each of the train's calls, as the game does.
    if (At<char*>(self, field::kObjectB0))
        if (Virtual<std::uint32_t>(At<char*>(self, field::kObjectB0), slot::kStateType) == 0x14u)
            if (char* const controls = Virtual<char*>(At<char*>(self, field::kObjectB0), slot::kControls))
                if (At<void*>(controls, 0x28)) {
                    std::uint32_t trainId = Virtual<std::uint32_t>(At<char*>(self, field::kObjectB0), slot::kId);
                    Virtual<void>(At<void*>(entered, 0x28), slot::kRecordSlot13, 0x11u, &trainId, 1u);
                }
    if (Bool(Virtual<std::uint32_t>(self, slot::kIsTrain))) {
        const std::uint32_t fill = Global<std::uint32_t>(addr::cSC4TrainCar_DefaultFloat);
        std::uint32_t point[3] = {fill, fill, fill};
        Virtual<std::uint32_t>(self, slot::kCollide, 0u, static_cast<void*>(point));
    }
    return ok;
}

// 0x00557530, the train's slot 110 (state), ret 4: the car's first. For the
// states 0x10 and 0x1E each car not in that state already (slot 59) pushes
// it (slot 52: the type, 0, -1.0, four zeros). For 0x10 the state's +0x30
// zero and, with a template, +0x34 its entry's +8 (1.0 without an entry).
// AL the car's.
std::uint32_t __fastcall TrainEnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("TrainEnterState", logged);
    const std::uint8_t ok = Bool(CallThis<std::uint32_t>(addr::cSC4TrainCar_EnterState, self, entered));
    if (!entered)
        return ok;
    const std::uint32_t type = At<std::uint32_t>(entered, 4);
    if (type == 0x10u || type == 0x1Eu)
        ForEachCar(self, [&](char* car) {
            if (!car)
                return;
            const std::uint32_t now = At<std::uint32_t>(entered, 4);
            if (Virtual<std::uint32_t>(car, slot::kStateType) != now)
                Virtual<std::uint32_t>(car, slot::kPushState, now, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
        });
    if (At<std::uint32_t>(entered, 4) != 0x10u)
        return ok;
    At<std::uint32_t>(entered, 0x30) = 0;
    char* const owner = Virtual<char*>(self, slot::kTemplate);
    if (!owner)
        return ok;
    char* entry = At<char*>(owner, 0x64);
    if (!entry)
        if (char* const parent = At<char*>(owner, 0xC))
            entry = CallThis<char*>(addr::SC4AutomatonTemplate_FindInherited64, parent);
    At<std::uint32_t>(entered, 0x34) = entry ? At<std::uint32_t>(entry, 8) : 0x3F800000u;
    return ok;
}

// 0x00557620, the train's slot 111 (state), ret 4: for the state 0x1E each
// car in it (slot 59) pops it (slot 53); then the base's slot 111
// (0x005535D0), its EAX.
std::uint32_t __fastcall TrainLeaveState(char* self, void*, char* left)
{
    static bool logged;
    FirstCall("TrainLeaveState", logged);
    if (left && At<std::uint32_t>(left, 4) == 0x1Eu)
        ForEachCar(self, [&](char* car) {
            if (car && Virtual<std::uint32_t>(car, slot::kStateType) == At<std::uint32_t>(left, 4))
                Virtual<std::uint32_t>(car, slot::kPopState);
        });
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_LeaveState, self, left);
}

// 0x00558DC0, the train's slot 73 (other), ret 4: AL 0 when the other is
// one of its cars (SC4::FindPointer); else the core's slot 73 (0x005059C0),
// its EAX.
std::uint32_t __fastcall TrainCanMeet(char* self, void*, void* other)
{
    static bool logged;
    FirstCall("TrainCanMeet", logged);
    void** const end = At<void**>(self, train::kCars + 4);
    // Both pointer arguments are the other's stack slot, as the game's.
    void** const found = CallCdecl<void**>(addr::SC4_FindPointer, At<void**>(self, train::kCars), end,
                                           static_cast<void*>(&other), static_cast<void*>(&other));
    if (found != end)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_CanMeet, self, other);
}

// 0x005570C0, the train's slot 121 (state, seconds), ret 8: the core's
// (0x00509070) first; with the template's entry the state's speed +0x28
// up by seconds x 0.001 x the entry's +0x10 while +0x7C is 0 or more, else
// down by it x the entry's +0x14 while +0x80 is, else zero. AL 0 without an
// entry, else 1.
std::uint32_t __fastcall TrainSteer(char* self, void*, char* state, float seconds)
{
    static bool logged;
    FirstCall("TrainSteer", logged);
    CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Steer, self, state, seconds);
    const float scaled = static_cast<float>(static_cast<double>(seconds) *
                                            static_cast<double>(Global<float>(addr::cSC4AutomatonBase_ArriveDistance)));
    char* const entry = TemplateEntry(self);
    if (!entry)
        return 0;
    float& speed = At<float>(state, 0x28);
    // FCOMP and TEST AH,1: below or unordered goes on to the next test.
    if (At<float>(state, 0x7C) >= Zero())
        speed = static_cast<float>(static_cast<double>(scaled) * At<float>(entry, 0x10) + speed);
    else if (At<float>(state, 0x80) >= Zero())
        speed = static_cast<float>(speed - static_cast<double>(scaled) * At<float>(entry, 0x14));
    else
        At<std::uint32_t>(state, 0x28) = 0;
    return 1;
}

// The slope part of 0x00557830 (from 0x0055785C), in x87 as the game's: the
// game keeps asin(y) x most / divisor and the result at the FPU's precision
// and rounds once, where doubles round each step (a last-bit difference on
// some headings). `y` is the heading's y (re-read after the asin, as the
// game does), `factor` the rate as a float; the answer in ST0. The globals:
// 0x00A81054 zero, 0x00A81228 one, 0x00B21CFC the most, 0x00B21CF8 the
// divisor; 0x009F03B0 __CIasin.
__declspec(naked) double __cdecl X87Slope(const float* y, const float* factor)
{
    __asm {
        mov eax, [esp + 4]
        fld dword ptr [eax]
        mov eax, 0x009F03B0
        call eax
        mov eax, [esp + 4]
        mov edx, 0x00A81054
        fld dword ptr [eax]
        fcomp dword ptr [edx]
        fnstsw ax
        test ah, 0x41
        jnz not_up
        mov edx, 0x00B21CFC
        fmul dword ptr [edx]
        mov edx, 0x00B21CF8
        fdiv dword ptr [edx]
        mov edx, 0x00A81054
        fcom dword ptr [edx]
        fnstsw ax
        test ah, 5
        jp up_clamp
        fstp st(0)
        fld dword ptr [edx]
        mov edx, 0x00A81228
        fsubr dword ptr [edx]
        mov ecx, [esp + 8]
        fmul dword ptr [ecx]
        ret
    up_clamp:
        mov edx, 0x00B21CFC
        fcom dword ptr [edx]
        fnstsw ax
        test ah, 0x41
        jnz up_keep
        fstp st(0)
        fld dword ptr [edx]
    up_keep:
        mov edx, 0x00A81228
        fsubr dword ptr [edx]
        mov ecx, [esp + 8]
        fmul dword ptr [ecx]
        ret
    not_up:
        mov eax, [esp + 4]
        mov edx, 0x00A81054
        fld dword ptr [eax]
        fcomp dword ptr [edx]
        fnstsw ax
        test ah, 5
        jp flat
        mov edx, 0x00B21CFC
        fmul dword ptr [edx]
        mov edx, 0x00B21CF8
        fdiv dword ptr [edx]
        mov edx, 0x00A81054
        fcom dword ptr [edx]
        fnstsw ax
        test ah, 5
        jp down_clamp
        fstp st(0)
        fld dword ptr [edx]
        mov edx, 0x00A81228
        fadd dword ptr [edx]
        mov ecx, [esp + 8]
        fmul dword ptr [ecx]
        ret
    down_clamp:
        mov edx, 0x00B21CFC
        fcom dword ptr [edx]
        fnstsw ax
        test ah, 0x41
        jnz down_keep
        fstp st(0)
        fld dword ptr [edx]
    down_keep:
        mov edx, 0x00A81228
        fadd dword ptr [edx]
        mov ecx, [esp + 8]
        fmul dword ptr [ecx]
        ret
    flat:
        fstp st(0)
        mov ecx, [esp + 8]
        fld dword ptr [ecx]
        ret
    }
}

// 0x00557830, the train's slot 125, a double in ST0: the core's rate
// (0x00505AB0) as a float; with an occupant and without the automata flag
// 0x40 the slope as cSC4Vehicle's slot 125 (X87Slope): asin of the
// heading's y x 0x00B21CFC / 0x00B21CF8 (at most 0x00B21CFC, below 0 as 0),
// 1 less it uphill, 1 plus it downhill (where it is below 0 for a positive
// tuning ratio, so 1), times the rate unrounded.
double __fastcall TrainSpeedFactor(char* self, void*)
{
    static bool logged;
    FirstCall("TrainSpeedFactor", logged);
    const float factor = static_cast<float>(CallThis<double>(addr::cSC4AutomatonCore_StateRate, self));
    if (!At<void*>(self, field::kOccupant) || Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, 0x40u)))
        return factor;
    return X87Slope(&At<float>(self, field::kHeading + 4), &factor);
}

// The train's and the mass transit train's slot 172 (a, b, c, d), ret 0x10:
// with c, c's slot 48, that one's slot 3 in one of the groups (slot 22) and
// slot 173 (a, b) true, the state 0x1E pushed (slot 52: 0x1E, 0, 5000.0,
// zero, zero, the word given, zero) and AL 1; else `fallback`.
std::uint32_t StopAt(char* self, std::uint32_t a, std::uint32_t b, void* c, std::uint32_t d,
                     std::initializer_list<std::uint32_t> groups, std::uint32_t word, std::uintptr_t fallback)
{
    if (c)
        if (void* const next = Virtual<void*>(c, slot::kNextAutomaton)) {
            void* const part = Virtual<void*>(next, 0x0C);
            bool in = false;
            for (const std::uint32_t group : groups)
                if (Bool(Virtual<std::uint32_t>(part, slot::kInGroup, group))) {
                    in = true;
                    break;
                }
            if (in && Bool(Virtual<std::uint32_t>(self, slot::kSlot173, a, b))) {
                Virtual<std::uint32_t>(self, slot::kPushState, 0x1Eu, 0u, 0x459C4000u, 0u, 0u, word, 0u);
                return 1;
            }
        }
    return CallThis<std::uint32_t>(fallback, self, a, b, c, d);
}

// 0x00557170, the train's: the groups 0x1300, 0x1302, 0x1509; the base's
// slot 172 (0x0054D830) else.
std::uint32_t __fastcall TrainSlot172(char* self, void*, std::uint32_t a, std::uint32_t b, void* c, std::uint32_t d)
{
    static bool logged;
    FirstCall("TrainSlot172", logged);
    return StopAt(self, a, b, c, d, {0x1300u, 0x1302u, 0x1509u}, 0u, addr::cSC4AutomatonBase_Slot172);
}

// 0x0053C970, the mass transit train's: the groups 0x1303, 0x1307, the word
// 0x0C16DC50; the train's slot 172 else.
std::uint32_t __fastcall MassTransitSlot172(char* self, void*, std::uint32_t a, std::uint32_t b, void* c,
                                            std::uint32_t d)
{
    static bool logged;
    FirstCall("MassTransitSlot172", logged);
    return StopAt(self, a, b, c, d, {0x1303u, 0x1307u}, 0x0C16DC50u, addr::cSC4Train_Slot172);
}

// The cells of a deque's elements told to the network service (slot 38) as
// LeaveCells tells slot 39 (the finish re-read each step).
void EnterCells(char* self, char* deque, std::size_t size, std::size_t block)
{
    char* current = At<char*>(deque, 0);
    char* last = At<char*>(deque, 8);
    char** node = At<char**>(deque, 0xC);
    while (current != At<char*>(deque, 0x10)) {
        if (void* const service = Global<void*>(addr::SC4_Service43D54)) {
            std::int32_t first = 0;
            std::int32_t second = 0;
            Virtual<void>(At<void*>(current, 0), slot::kCell, &first, &second);
            Virtual<void>(service, slot::kEnterCell, static_cast<void*>(self), first, second);
        }
        current += size;
        if (current == last) {
            ++node;
            current = *node;
            last = current + block;
        }
    }
}

// 0x00558FA0, the train's slot 165: the base's (0x00553AA0) first. With a
// wanted count and steps left in the +0x204 deque: when it was empty before,
// the cells of both deques' occupants entered (EnterCells, the network
// service's slot 38); else, with the service, that of the +0x204 deque's
// last step (0x00533290). AL the base's.
std::uint32_t __fastcall TrainNextStep(char* self, void*)
{
    static bool logged;
    FirstCall("TrainNextStep", logged);
    char* const steps = self + field::kDeque204;
    const bool wasEmpty = At<char*>(steps, 0x10) == At<char*>(steps, 0);
    const std::uint8_t ok = Bool(CallThis<std::uint32_t>(addr::cSC4AutomatonBase_NextStep, self));
    if (!At<std::uint32_t>(self, train::kWanted) || At<char*>(steps, 0x10) == At<char*>(steps, 0))
        return ok;
    if (wasEmpty) {
        EnterCells(self, steps, 0xC, 0x78);
        EnterCells(self, self + field::kDeque27C, 8, 0x80);
        return ok;
    }
    if (void* const service = Global<void*>(addr::SC4_Service43D54)) {
        char* const element = CallThis<char*>(addr::SC4AutomatonDeque204_Back, steps);
        std::int32_t first = 0;
        std::int32_t second = 0;
        Virtual<void>(At<void*>(element, 0), slot::kCell, &first, &second);
        Virtual<void>(service, slot::kEnterCell, static_cast<void*>(self), first, second);
    }
    return ok;
}

// 0x0053CA20, the mass transit train's slot 165: the train's first; unless
// in the state 0x10 or with the automata flag 1 (slot 31), with the lot
// manager 0x00B43D08: the front point's (+0x264, +0x26C) cell (city slot
// 95) and the next one by the direction byte +0x1F4 (0: x - 1, 1: z - 1, 2:
// x + 1, 3: z + 1, else the same); a lot at the cell (slot 6) and none at
// the next push the state 0x1E (0, 5000.0, zero, zero, 0x0C16DC50, zero).
// AL the train's.
std::uint32_t __fastcall MassTransitNextStep(char* self, void*)
{
    static bool logged;
    FirstCall("MassTransitNextStep", logged);
    const std::uint8_t ok = Bool(CallThis<std::uint32_t>(addr::cSC4Train_NextStep, self));
    if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x10u || Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, 1u)))
        return ok;
    void* const lots = Global<void*>(addr::SC4_LotManager);
    if (!lots)
        return ok;
    std::int32_t x = 0;
    std::int32_t z = 0;
    Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), slot::kCellOf, At<float>(self, field::kFront),
                           At<float>(self, field::kFront + 8), &x, &z);
    std::int32_t nx = x;
    std::int32_t nz = z;
    switch (At<std::uint8_t>(self, field::kDirection)) {
    case 0:
        nx = x - 1;
        break;
    case 1:
        nz = z - 1;
        break;
    case 2:
        nx = x + 1;
        break;
    case 3:
        nz = z + 1;
        break;
    default:
        break;
    }
    if (Virtual<void*>(lots, slot::kGetLot, x, z, 0u) && !Virtual<void*>(lots, slot::kGetLot, nx, nz, 0u))
        Virtual<std::uint32_t>(self, slot::kPushState, 0x1Eu, 0u, 0x459C4000u, 0u, 0u, 0x0C16DC50u, 0u);
    return ok;
}

// 0x00558E00, the train's slot 65 (a, b; neither read), ret 8: the state
// 0x14 pushed unless in it (slot 52: 0x14, 0, 20000.0, 1, three zeros; AL 0
// when that fails). A generator of its own (SC4Random::Construct, -1); per
// car (no null test): the train's speed +0x94 copied, a random unit vector
// (SC4::RandomUnitVector) with its x and z scaled by 0.25 over its length
// when that is above 0 and its y dropped, added to the car's heading, the
// heading normalised (1.0 over its length, no zero test); a car already in
// the state 0x14 answers AL 1 at once, else it pushes it (a failure makes
// AL 0). Then slot 36 (0, 0). AL 1 unless a push failed.
std::uint32_t __fastcall TrainCollide(char* self, void*, std::uint32_t, std::uint32_t)
{
    static bool logged;
    FirstCall("TrainCollide", logged);
    std::uint8_t ok = 1;
    if (Virtual<std::uint32_t>(self, slot::kStateType) != 0x14u &&
        !Bool(Virtual<std::uint32_t>(self, slot::kPushState, 0x14u, 0u, 0x469C4000u, 1u, 0u, 0u, 0u)))
        ok = 0;
    alignas(8) char generator[0x18] = {};
    {
        // The clock seed is counted under the original's call site (the
        // return address after its cRZRandom call), not ours.
        const fixed_seed::SiteOverride site(0x00558E44);
        CallThis<void>(addr::SC4Random_Construct, generator, 0xFFFFFFFFu);
    }
    for (char** at = At<char**>(self, train::kCars); at != At<char**>(self, train::kCars + 4); ++at) {
        char* const car = *at;
        At<std::uint32_t>(car, field::kSpeed) = At<std::uint32_t>(self, field::kSpeed);
        float v[3];
        CallCdecl<void*>(addr::SC4_RandomUnitVector, static_cast<void*>(v), static_cast<void*>(generator));
        const double square = (static_cast<double>(v[2]) * v[2] + static_cast<double>(v[1]) * v[1]) +
                              static_cast<double>(v[0]) * v[0];
        if (square > Zero()) {
            const double k = Global<float>(addr::cSC4AutomatonBase_Quarter) / std::sqrt(square);
            v[0] = static_cast<float>(v[0] * k);
            v[2] = static_cast<float>(v[2] * k);
        }
        v[1] = 0.0f;
        float* const heading = &At<float>(car, field::kHeading);
        heading[0] = static_cast<float>(static_cast<double>(v[0]) + heading[0]);
        heading[1] = static_cast<float>(static_cast<double>(v[1]) + heading[1]);
        heading[2] = static_cast<float>(static_cast<double>(v[2]) + heading[2]);
        const double x = heading[0], y = heading[1], z = heading[2];
        const double k = Global<float>(addr::cSC4TrainCar_One) / std::sqrt((x * x + y * y) + z * z);
        heading[0] = static_cast<float>(k * heading[0]);
        heading[1] = static_cast<float>(k * heading[1]);
        heading[2] = static_cast<float>(k * heading[2]);
        if (Virtual<std::uint32_t>(car, slot::kStateType) == 0x14u)
            return 1;
        if (!Bool(Virtual<std::uint32_t>(car, slot::kPushState, 0x14u, 0u, 0x469C4000u, 1u, 0u, 0u, 0u)))
            ok = 0;
    }
    Virtual<std::uint32_t>(self, slot::kSetSpeed, 0.0f, 0.0f);
    return ok;
}

// 0x00557320, the car's slot 132 (occupant, groups, path, keep), ret 0x10:
// cSC4Vehicle's (0x0055D5A0); with the byte +0x2B8 and an occupant there,
// its interface 0x4A10AFC2 (QueryInterface): slot 33 (1.0, 0.1) when its
// slot 99 (an out word) answers 2, else (0, 0.6); the interface released.
void __fastcall CarEnterOccupant(char* self, void*, void* occupant, std::uint32_t groups, void* path,
                                 std::uint32_t keep)
{
    static bool logged;
    FirstCall("CarEnterOccupant", logged);
    CallThis<void>(addr::cSC4Vehicle_EnterOccupant, self, occupant, groups, path, keep);
    void* const now = At<void*>(self, field::kOccupant);
    if (!At<std::uint8_t>(self, car::kGroupByte) || !now)
        return;
    void* network = nullptr;
    if (!Bool(Virtual<std::uint32_t>(now, 0x00, kNetworkIid, &network)))
        return;
    std::uint32_t out = keep; // the game's out is the keep argument's stack slot
    if (Virtual<std::uint32_t>(network, slot::kNetworkKind, &out) == 2u)
        Virtual<std::uint32_t>(self, slot::kFade, 1.0f, 0.1f);
    else
        Virtual<std::uint32_t>(self, slot::kFade, 0.0f, 0.6f);
    Release(network);
}

// 0x005574F0, the train's slot 132: slot 181 with the deque +0x27C and twice
// the wanted count (the low byte doubled, as SHL CL), then the car's slot
// 132 (0x00557320).
void __fastcall TrainEnterOccupant(char* self, void*, void* occupant, std::uint32_t groups, void* path,
                                   std::uint32_t keep)
{
    static bool logged;
    FirstCall("TrainEnterOccupant", logged);
    const std::uint32_t count = (At<std::uint8_t>(self, train::kWanted) << 1) & 0xFFu;
    Virtual<std::uint32_t>(self, slot::kRemember, self + field::kDeque27C, count);
    CallThis<void>(addr::cSC4TrainCar_EnterOccupant, self, occupant, groups, path, keep);
}

// ====================================================================================
// The movement (tr2b)

void* City() { return Global<void*>(addr::SC4_City); }
double One() { return Global<float>(addr::cSC4TrainCar_One); }
double Half() { return Global<float>(addr::SC4_FloatHalf); }
void CopyWords(void* to, const void* from) { std::memcpy(to, from, 12); }

std::uint32_t Fill() { return Global<std::uint32_t>(addr::cSC4TrainCar_DefaultFloat); }

// The points of a path (begin, end; 12 bytes each), the count as the game
// divides it.
std::int32_t PointCount(char** path) { return static_cast<std::int32_t>(path[1] - path[0]) / 12; }

void SetFlag(char* self, std::uint32_t flag, std::uint32_t on) { Virtual<void>(self, slot::kSetFlag, flag, on); }
bool CheckFlag(char* self, std::uint32_t flag) { return Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, flag)) != 0; }

// A NaN as the game tests a word: the exponent all ones and a mantissa.
bool IsNan(const float* f)
{
    std::uint32_t word;
    std::memcpy(&word, f, 4);
    return (word & 0x7F800000u) == 0x7F800000u && (word & 0x7FFFFFu) != 0;
}

// The destination +0x4C half of `d` behind `point`: x unrounded, y and z
// through floats.
void HalfBehind(char* self, const float* point, const float* d)
{
    const double hx = d[0] * Half();
    const float hy = static_cast<float>(d[1] * Half());
    const float hz = static_cast<float>(d[2] * Half());
    float* const destination = &At<float>(self, field::kDestination);
    destination[2] = static_cast<float>(static_cast<double>(point[2]) - hz);
    destination[0] = static_cast<float>(point[0] - hx);
    destination[1] = static_cast<float>(static_cast<double>(point[1]) - hy);
}

// 0x00558510, the train's slot 129 (frame seconds), ret 4: the step (slot
// 125 x the smoothed speed x seconds) and +0x94 the smoothed speed. With slot
// 88, an empty route and the automata flag 0x20 slot 79 and true. Else slot
// 161, the room ahead (slot 151; below 16 with +0xB0 SetSpeed to +0xB0's
// speed at 0.5) caps the step; at a step not above 0 the flag 0x20 set, +0x94
// zero and, when minus the radius is above the step and the flag 0x40 is off,
// Remove(false) and false. Else the front point +0x264 (the position when a
// coordinate is below 0) along the path (slot 133) into a new point: when
// that fails the flag 4 and slot 102 (4, 0); else the rear +0x2BC from slot
// 182 (the new point, the radius, +0x27C) and the direction the front point
// less the rear; below 1e-5 +0x270, true at once when that is the core's
// default point (within 1e-4) or below 1e-5 too. The destination half of it
// behind the new point, slot 16 with its unit vector; +0x270 that, +0x264
// the new point, the flag 0x20 cleared. AL only.
std::uint8_t __fastcall TrainMoveOnPath(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("TrainMoveOnPath", logged);
    Virtual<void*>(City(), slot::kTrafficNetwork);
    const float speed = static_cast<float>(Virtual<double>(self, slot::kSmoothedNow));
    const double factor = Virtual<double>(self, slot::kSlot125);
    At<float>(self, field::kSpeed) = speed;
    float step = static_cast<float>((factor * speed) * seconds);
    if (Bool(Virtual<std::uint32_t>(self, slot::kSlot88)) &&
        At<char*>(self, field::kRouteFinish) == At<char*>(self, field::kRoute) && CheckFlag(self, 0x20u)) {
        Virtual<std::uint32_t>(self, slot::kSlot79);
        return 1;
    }
    Virtual<std::uint32_t>(self, slot::kSlot161);
    const float clear = static_cast<float>(Virtual<double>(self, slot::kClearAhead));
    if (void* const ahead = At<void*>(self, field::kObjectB0))
        if (clear < Global<float>(addr::SC4_FloatSixteen)) {
            const float target = static_cast<float>(Virtual<double>(ahead, slot::kSmoothedNow));
            Virtual<std::uint32_t>(self, slot::kSetSpeed, target, 0.5f);
        }
    if (!(step < clear))
        step = clear;
    if (!(step > Zero())) {
        SetFlag(self, 0x20u, 1u);
        At<std::uint32_t>(self, field::kSpeed) = 0;
        const double radius = Virtual<double>(self, slot::kGetRadius);
        if (-radius > step && !CheckFlag(self, 0x40u)) {
            Virtual<std::uint32_t>(self, slot::kRemove, 0u);
            return 0;
        }
        return 1;
    }
    float* const front = &At<float>(self, field::kFront);
    if (front[0] < Zero() || front[1] < Zero() || front[2] < Zero())
        CopyWords(front, &At<float>(self, field::kPosition));
    std::uint32_t words[3] = {Fill(), Fill(), Fill()};
    float* const next = reinterpret_cast<float*>(words);
    if (!Bool(Virtual<std::uint32_t>(self, slot::kAdvanceOnPath, step, static_cast<const float*>(front), next,
                                     &At<std::int32_t>(self, field::kPathIndex), At<char**>(self, field::kPath),
                                     &At<float>(self, field::kFloat260)))) {
        SetFlag(self, 4u, 1u);
        Virtual<std::uint32_t>(self, slot::kSlot102, 4u, 0u);
        return 1;
    }
    const float* const rear = &At<float>(self, car::kFloats);
    Virtual<std::uint32_t>(self, slot::kTrail, static_cast<const float*>(next), &At<float>(self, car::kFloats),
                           At<float>(self, field::kRadius), self + field::kDeque27C);
    // The x87 keeps x and z unrounded; y goes through a float.
    double dx = static_cast<double>(front[0]) - rear[0];
    float dy = static_cast<float>(static_cast<double>(front[1]) - rear[1]);
    double dz = static_cast<double>(front[2]) - rear[2];
    double length = std::sqrt((dz * dz + static_cast<double>(dy) * dy) + dx * dx);
    const float epsilon = Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon);
    if (length < epsilon) {
        const float* const travel = &At<float>(self, field::kTravel);
        const float* const unset = reinterpret_cast<const float*>(addr::cSC4AutomatonCore_DefaultPoint);
        const float within = Global<float>(addr::cSC4Train_Near);
        dx = travel[0];
        dy = travel[1];
        dz = travel[2];
        if (std::fabs(dx - unset[0]) < within && std::fabs(static_cast<double>(dy) - unset[1]) < within &&
            std::fabs(dz - unset[2]) < within)
            return 1;
        length = std::sqrt((dz * dz + static_cast<double>(dy) * dy) + dx * dx);
        if (length < epsilon)
            return 1;
    }
    const double hx = dx * Half();
    const float hy = static_cast<float>(static_cast<double>(dy) * Half());
    const float hz = static_cast<float>(dz * Half());
    float* const destination = &At<float>(self, field::kDestination);
    destination[2] = static_cast<float>(static_cast<double>(next[2]) - hz);
    destination[0] = static_cast<float>(next[0] - hx);
    destination[1] = static_cast<float>(static_cast<double>(next[1]) - hy);
    const double inverse = One() / length;
    float heading[3];
    heading[0] = static_cast<float>(inverse * dx);
    heading[1] = static_cast<float>(static_cast<double>(dy) * inverse);
    heading[2] = static_cast<float>(dz * inverse);
    Virtual<std::uint32_t>(self, slot::kPlace, static_cast<const float*>(destination),
                           static_cast<const float*>(heading));
    CopyWords(&At<float>(self, field::kTravel), heading);
    CopyWords(front, next);
    SetFlag(self, 0x20u, 0u);
    return 1;
}

// 0x005588D0, the train's slot 131 (occupant, key, exact), ret 0xC: with
// 0x00B43DA8's slot 17 true for the occupant's cell (slot 59, no null tests)
// the vehicle manager's records given the event (SC4::ApplyTrafficEvent: +4,
// 2, 20000) and, with the message server, a message 0x0C05E4CD with the
// cell posted (the message's new not tested). Then the base's (0x0054E920);
// with it true, a path and no automata flag 0x2000: not exact, the front
// point +0x264 the path's last and +0x1FC the count less 2; exact, with
// +0x260 above 0 +0x260 capped by the room ahead (slot 151) and the path's
// first point advanced by it (slot 133, its answer dropped), else the
// network part's index of the front point (slot 19, dropped). The rear
// (slot 182: the front point, the radius, +0x27C); the direction the front
// point less the rear, or with a NaN in the rear the radius times the unit
// vector (a float inverse) of the path's first point to its last (false
// without a path; the front point then the last); the destination half of
// it behind the front point; slot 16 with its unit vector (null at 1e-5 or
// less); +0x270 that; the flag 0x20 cleared. AL the base's.
std::uint8_t __fastcall TrainPathTo(char* self, void*, void* occupant, std::uint32_t key, std::uint32_t exact)
{
    static bool logged;
    FirstCall("TrainPathTo", logged);
    std::int32_t first = 0; // stack garbage in the game; slot 59 sets them
    std::int32_t second = 0;
    Virtual<void>(occupant, slot::kCell, &first, &second);
    if (Bool(Virtual<std::uint32_t>(Global<void*>(addr::SC4_Service43DA8), slot::kEventAt, first, second))) {
        void* const manager = Virtual<void*>(self, slot::kManager);
        CallThis<void>(addr::SC4_ApplyTrafficEvent, manager, static_cast<void*>(self + kVtable4), 2u, 0x4E20u);
        if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
            void* const memory = CallCdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
            void* const message = memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
            if (message)
                Virtual<std::uint32_t>(message, kAddRef);
            Virtual<std::uint32_t>(message, slot::kSetType, 0x0C05E4CDu);
            Virtual<std::uint32_t>(message, slot::kSetData1, first);
            Virtual<std::uint32_t>(message, slot::kSetData2, second);
            Virtual<std::uint32_t>(server, slot::kPost, message, 0u);
            Release(message);
        }
    }
    const std::uint8_t answer = CallThis<std::uint8_t>(addr::cSC4AutomatonBase_PathTo, self, occupant, key, exact);
    if (!answer || !At<char**>(self, field::kPath) || CheckFlag(self, 0x2000u))
        return answer;
    float* const front = &At<float>(self, field::kFront);
    float& leftover = At<float>(self, field::kFloat260);
    if (!static_cast<std::uint8_t>(exact)) {
        char** const path = At<char**>(self, field::kPath);
        const std::int32_t count = PointCount(path);
        At<std::int32_t>(self, field::kPathIndex) = count - 2;
        CopyWords(front, path[0] + 12 * count - 12);
    } else if (!(leftover > Zero())) {
        Virtual<std::int32_t>(At<void*>(self, field::kNetwork), slot::kIndexOf, front, At<char**>(self, field::kPath));
    } else {
        const double clear = Virtual<double>(self, slot::kClearAhead);
        const float step = leftover < clear ? leftover : static_cast<float>(clear);
        leftover = step;
        char** const path = At<char**>(self, field::kPath);
        std::uint32_t from[3];
        CopyWords(from, path[0]);
        Virtual<std::uint32_t>(self, slot::kAdvanceOnPath, step, reinterpret_cast<const float*>(from), front,
                               &At<std::int32_t>(self, field::kPathIndex), path, &leftover);
    }
    float* const rear = &At<float>(self, car::kFloats);
    Virtual<std::uint32_t>(self, slot::kTrail, static_cast<const float*>(front), rear, At<float>(self, field::kRadius),
                           self + field::kDeque27C);
    std::uint32_t words[3] = {Fill(), Fill(), Fill()};
    float* const d = reinterpret_cast<float*>(words);
    if (IsNan(rear) || IsNan(rear + 1) || IsNan(rear + 2)) {
        char** const path = At<char**>(self, field::kPath);
        if (!path)
            return answer;
        CopyWords(front, path[1] - 12);
        const float* const start = reinterpret_cast<const float*>(At<char**>(self, field::kPath)[0]);
        const double dx = static_cast<double>(front[0]) - start[0];
        const double dy = static_cast<double>(front[1]) - start[1];
        const double dz = static_cast<double>(front[2]) - start[2];
        const float dzf = static_cast<float>(dz);
        const float inverse = static_cast<float>(One() / std::sqrt((dz * dzf + dy * dy) + dx * dx));
        const float ux = static_cast<float>(dx * inverse);
        const float uy = static_cast<float>(dy * inverse);
        const double uz = static_cast<double>(dzf) * inverse;
        const double radius = At<float>(self, field::kRadius);
        d[0] = static_cast<float>(radius * ux);
        d[1] = static_cast<float>(uy * radius);
        d[2] = static_cast<float>(uz * radius);
    } else {
        d[0] = static_cast<float>(static_cast<double>(front[0]) - rear[0]);
        d[1] = static_cast<float>(static_cast<double>(front[1]) - rear[1]);
        d[2] = static_cast<float>(static_cast<double>(front[2]) - rear[2]);
    }
    HalfBehind(self, front, d);
    const double length = std::sqrt((static_cast<double>(d[2]) * d[2] + static_cast<double>(d[1]) * d[1]) +
                                    static_cast<double>(d[0]) * d[0]);
    float* const destination = &At<float>(self, field::kDestination);
    if (!(length > Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon))) {
        Virtual<std::uint32_t>(self, slot::kPlace, static_cast<const float*>(destination),
                               static_cast<const float*>(nullptr));
    } else {
        const double inverse = One() / length;
        d[0] = static_cast<float>(d[0] * inverse);
        d[1] = static_cast<float>(d[1] * inverse);
        d[2] = static_cast<float>(d[2] * inverse);
        Virtual<std::uint32_t>(self, slot::kPlace, static_cast<const float*>(destination), static_cast<const float*>(d));
    }
    CopyWords(&At<float>(self, field::kTravel), d);
    SetFlag(self, 0x20u, 0u);
    return answer;
}

// 0x0055AD00, the train's slot 134: false without the traffic network map's
// occupant at the position's cell (city slot 95, map slot 11) or its network
// part (slot 49); slot 174 called, its answer not read. The kind 4 with the
// byte +0x2B8, else 6 in the group 0x422D or 0x422C, 7 in 0x422F or 0x422E,
// else 3; without the automata flag 0x4000, with +0x98 and +0xA4 longer than
// 1e-5, +0xA4 normalised and its best side (slot 171). The part's path
// (slot 5: the kind, 0xFF, the side, the position, an out word, 0; false
// without one); slot 132 (occupant, the out word, path,
// 1); the +0x27C deque cleared; +0x1FC the last point's index, the front
// point +0x264 the last point, the heading +0x58 the last less the first.
// The rear (slot 182); with it, slot 16 with the point half the front less
// the rear behind the front and that direction's unit vector (x by the
// inverse unrounded, y and z by it as a float); without, the unit vector (a
// float inverse) from the point before, the rear the radius behind along it
// and slot 16 with the point half the radius behind and the unit vector.
// True.
std::uint8_t __fastcall TrainPlaceOnNetwork(char* self, void*)
{
    static bool logged;
    FirstCall("TrainPlaceOnNetwork", logged);
    void* const map = Virtual<void*>(City(), slot::kTrafficNetwork);
    Virtual<void*>(self, slot::kManager);
    float* const position = &At<float>(self, field::kPosition);
    std::int32_t cellX = 0; // stack garbage in the game; city slot 95 sets them
    std::int32_t cellZ = 0;
    Virtual<std::uint32_t>(City(), slot::kCellOf, position[0], position[2], &cellX, &cellZ);
    void* const occupant = Virtual<void*>(map, slot::kOccupantAt, cellX, cellZ);
    if (!occupant)
        return 0;
    void* const network = Virtual<void*>(occupant, slot::kSetLeader);
    if (!network)
        return 0;
    std::uint32_t kind = 3;
    if (At<std::uint8_t>(self, car::kGroupByte))
        kind = 4;
    else if (InGroup(self, 0x422Du) || InGroup(self, 0x422Cu))
        kind = 6;
    else if (InGroup(self, 0x422Fu) || InGroup(self, 0x422Eu))
        kind = 7;
    std::uint32_t side = 0xFFu; // a byte with three bytes of the game's stack above it
    if (!CheckFlag(self, 0x4000u) && At<std::uint32_t>(self, field::k98)) {
        float* const v = &At<float>(self, field::kVectorA4);
        const double x = v[0], y = v[1], z = v[2];
        if (std::sqrt((x * x + y * y) + z * z) > Global<float>(addr::SC4_FloatHundredThousandth)) {
            const double inverse = One() / std::sqrt((x * x + y * y) + z * z);
            v[0] = static_cast<float>(inverse * v[0]);
            v[1] = static_cast<float>(inverse * v[1]);
            v[2] = static_cast<float>(inverse * v[2]);
            Virtual<std::uint32_t>(self, slot::kBestDirection, occupant, static_cast<const float*>(v),
                                   reinterpret_cast<std::uint8_t*>(&side), 1u);
        }
    }
    std::uint32_t out = 0; // stack garbage in the game; the part's slot 5 sets it
    char** const found = Virtual<char**>(network, slot::kFindPath, kind, 0xFFu, side,
                                         static_cast<const float*>(position), &out, 0u);
    if (!found)
        return 0;
    Virtual<std::uint32_t>(self, slot::kEnterOccupant, occupant, out, found, 1u);
    CallThis<void>(addr::SC4VehicleDeque27C_Clear, self + field::kDeque27C);
    std::uint32_t headingWords[3] = {Fill(), Fill(), Fill()};
    std::uint32_t pointWords[3] = {Fill(), Fill(), Fill()};
    float* const a = reinterpret_cast<float*>(headingWords);
    float* const b = reinterpret_cast<float*>(pointWords);
    char** const path = At<char**>(self, field::kPath);
    const std::int32_t index = PointCount(path) - 1;
    At<std::int32_t>(self, field::kPathIndex) = index;
    float* const front = &At<float>(self, field::kFront);
    CopyWords(front, path[0] + 12 * index);
    const float* const start = reinterpret_cast<const float*>(At<char**>(self, field::kPath)[0]);
    float* const heading = &At<float>(self, field::kHeading);
    heading[2] = static_cast<float>(static_cast<double>(front[2]) - start[2]);
    heading[0] = static_cast<float>(static_cast<double>(front[0]) - start[0]);
    heading[1] = static_cast<float>(static_cast<double>(front[1]) - start[1]);
    float* const rear = &At<float>(self, car::kFloats);
    if (!Bool(Virtual<std::uint32_t>(self, slot::kTrail, static_cast<const float*>(front), rear,
                                     At<float>(self, field::kRadius), self + field::kDeque27C))) {
        const float* const before = reinterpret_cast<const float*>(At<char**>(self, field::kPath)[0]) +
                                    3 * At<std::int32_t>(self, field::kPathIndex) - 3;
        const double dx = static_cast<double>(front[0]) - before[0];
        const double dy = static_cast<double>(front[1]) - before[1];
        const double dz = static_cast<double>(front[2]) - before[2];
        const float dzf = static_cast<float>(dz);
        const float inverse = static_cast<float>(One() / std::sqrt((dz * dzf + dy * dy) + dx * dx));
        a[0] = static_cast<float>(dx * inverse);
        a[1] = static_cast<float>(dy * inverse);
        a[2] = static_cast<float>(static_cast<double>(dzf) * inverse);
        const double radius = At<float>(self, field::kRadius);
        const float ax = static_cast<float>(a[0] * radius);
        const float ay = static_cast<float>(a[1] * radius);
        const double az = radius * a[2];
        rear[2] = static_cast<float>(front[2] - az);
        rear[0] = static_cast<float>(static_cast<double>(front[0]) - ax);
        rear[1] = static_cast<float>(static_cast<double>(front[1]) - ay);
        const double hx = (a[0] * radius) * Half();
        const float hy = static_cast<float>(static_cast<float>(a[1] * radius) * Half());
        const float hz = static_cast<float>(static_cast<float>(a[2] * radius) * Half());
        b[2] = static_cast<float>(static_cast<double>(front[2]) - hz);
        b[0] = static_cast<float>(front[0] - hx);
        b[1] = static_cast<float>(static_cast<double>(front[1]) - hy);
    } else {
        const double dx = static_cast<double>(front[0]) - rear[0];
        const double dy = static_cast<double>(front[1]) - rear[1];
        const float dzf = static_cast<float>(static_cast<double>(front[2]) - rear[2]);
        const double hx = dx * Half();
        const float hy = static_cast<float>(dy * Half());
        const float hz = static_cast<float>(static_cast<double>(dzf) * Half());
        b[2] = static_cast<float>(static_cast<double>(front[2]) - hz);
        b[0] = static_cast<float>(front[0] - hx);
        b[1] = static_cast<float>(static_cast<double>(front[1]) - hy);
        const double inverse = One() / std::sqrt((static_cast<double>(dzf) * dzf + dy * dy) + dx * dx);
        const float rounded = static_cast<float>(inverse);
        a[0] = static_cast<float>(inverse * dx);
        a[1] = static_cast<float>(dy * rounded);
        a[2] = static_cast<float>(static_cast<double>(dzf) * rounded);
    }
    Virtual<std::uint32_t>(self, slot::kPlace, static_cast<const float*>(b), static_cast<const float*>(a));
    return 1;
}

// 0x00559C30, the train's slot 197 (direction), ret 4: the next occupant
// from 0x00B43D54's slot 15 (the occupant, the side +0x1F4, 0, +0x1F7 &
// 0xF; no null test of the service; false without one); its network part
// (slot 49) and slot 40's key (the occupant, the opposite side). With a key
// and a path, the part's path for the key (slot 3) when the path's last
// point is within 2 of its first (asked twice). Else the best side (slot 171
// with the direction) and the keys (slot 6: the kind, the path's last
// point, the opposite side, the side, 0.5): the path for the first, or with
// none slot 5's (the kind, the opposite side, 0xFF, the last point, the
// key's slot out, 0); the keys freed (SC4::Free). False without a path; else
// slot 131 (the occupant, the key, 1), its cell entered (slot 59, the
// service's slot 38) and that of the first occupant of the service's slot
// 15 (the occupant, the key, 0, 0) when there is one. True. The sides are
// bytes with three of the game's stack bytes above them (ours zero).
std::uint8_t __fastcall TrainNextOccupant(char* self, void*, const float* direction)
{
    static bool logged;
    FirstCall("TrainNextOccupant", logged);
    void* const service = Global<void*>(addr::SC4_Service43D54);
    const std::uint32_t side = At<std::uint8_t>(self, field::kDirection);
    const std::uint32_t opposite = (side + 2) & 3;
    const std::uint32_t kind = At<std::uint8_t>(self, field::kKind1F7) & 0xFu;
    void** const next = Virtual<void**>(service, slot::kNeighbour, At<void*>(self, field::kOccupant), side, 0u, kind);
    if (!next)
        return 0;
    void* const occupant = *next;
    void* const network = Virtual<void*>(occupant, slot::kSetLeader);
    std::uint32_t key = Virtual<std::uint32_t>(service, slot::kNeighbourKey, occupant, opposite);
    char** path = nullptr;
    bool nearby = false;
    if (key && At<char**>(self, field::kPath))
        if (char** const there = Virtual<char**>(network, slot::kPartPath, key, 0u)) {
            const float* const a = reinterpret_cast<const float*>(there[0]);
            const float* const b = reinterpret_cast<const float*>(At<char**>(self, field::kPath)[1]) - 3;
            const double dx = static_cast<double>(a[0]) - b[0];
            const double dy = static_cast<double>(a[1]) - b[1];
            const double dz = static_cast<double>(a[2]) - b[2];
            nearby = !(std::sqrt((dz * dz + dy * dy) + dx * dx) > Global<float>(addr::cSC4Vehicle_Two));
        }
    if (nearby) {
        path = Virtual<char**>(network, slot::kPartPath, key, 0u);
    } else {
        std::uint32_t sideWord = side;
        Virtual<std::uint32_t>(self, slot::kBestDirection, occupant, direction,
                               reinterpret_cast<std::uint8_t*>(&sideWord), 1u);
        std::uint32_t* keys[3] = {};
        const float* const last = reinterpret_cast<const float*>(At<char**>(self, field::kPath)[1]) - 3;
        Virtual<std::uint32_t>(network, slot::kKeys, static_cast<void*>(keys), kind, last, opposite, sideWord, 0.5f);
        if (keys[0] != keys[1]) {
            key = *keys[0];
            path = Virtual<char**>(network, slot::kPartPath, key, 0u);
        } else {
            path = Virtual<char**>(network, slot::kFindPath, kind, opposite, 0xFFu, last, &key, 0u);
        }
        if (keys[0])
            CallCdecl<void>(addr::SC4_Free, static_cast<void*>(keys[0]));
    }
    if (!path)
        return 0;
    Virtual<std::uint32_t>(self, slot::kPathTo, occupant, key, 1u);
    std::int32_t first = 0; // stack slots in the game; slot 59 sets them
    std::int32_t second = 0;
    Virtual<void>(occupant, slot::kCell, &first, &second);
    Virtual<void>(service, slot::kEnterCell, static_cast<void*>(self), first, second);
    void** const after = Virtual<void**>(service, slot::kNeighbour, occupant, key, 0u, 0u);
    if (after && *after) {
        Virtual<void>(*after, slot::kCell, &first, &second);
        Virtual<void>(service, slot::kEnterCell, static_cast<void*>(self), first, second);
    }
    return 1;
}

// 0x0055A8B0, the train's slot 199: the last car (the train without cars;
// no null test) and its radius (slot 17); when twice the radius is no more
// than the distance of its rear +0x2BC to the train's point +0x2DC less half
// the radius (x through a float), and with the vehicle manager (slot 174)
// and a car group: the manager's slot 15 (the group, 0x421A for 0x4203 when
// one car short of the wanted count; the point, +0x84, a zeroed vector of
// the caller's not freed, 0, the byte +0x90, 0) and the car interface
// (0x4A416254) of what it makes. With one: the automata flag 2 cleared, its
// leader (slot 49) the last car, slot 16 (the point, the last car's heading
// +0x58), slot 132 (+0x2E8, +0x2EC, 0, 1), pushed on the cars
// (SC4PtrVector::PushBack, then AddRef) and the state 0x10 pushed (0, -1.0,
// four zeros) when the train is in it; the interface released. AL whether
// one was made.
std::uint8_t __fastcall TrainAddCar(char* self, void*)
{
    static bool logged;
    FirstCall("TrainAddCar", logged);
    void* car = nullptr;
    char** const cars = &At<char*>(self, train::kCars);
    char* const last = cars[0] == cars[1] ? self : At<char**>(self, train::kCars + 4)[-1];
    const double radius = Virtual<double>(last, slot::kGetRadius);
    const float* const rear = &At<float>(last, car::kFloats);
    float* const point = &At<float>(self, train::kPoint);
    const float dx = static_cast<float>(static_cast<double>(rear[0]) - point[0]);
    const double dy = static_cast<double>(rear[1]) - point[1];
    const double dz = static_cast<double>(rear[2]) - point[2];
    const double length = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
    if (radius + radius <= length - radius * Half()) {
        void* const vehicles = Virtual<void*>(self, slot::kManager);
        std::uint32_t group = At<std::uint32_t>(self, train::kCarGroup);
        if (vehicles && group) {
            if (group == 0x4203u && CarCount(self) == At<std::uint32_t>(self, train::kWanted) - 1)
                group = 0x421Au;
            std::uint32_t list[3] = {0, 0, 0};
            void* const made = Virtual<void*>(vehicles, slot::kMakeCar, group, point,
                                              At<std::uint32_t>(self, field::kAutomataFlags), static_cast<void*>(list),
                                              0u, static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kByte90)), 0u);
            if (made) {
                if (void* const held = car) {
                    car = nullptr;
                    Release(held);
                }
                if (!Bool(Virtual<std::uint32_t>(made, 0x00, kCarClassId, &car)))
                    if (void* const held = car) {
                        car = nullptr;
                        Release(held);
                    }
            }
            if (car) {
                Virtual<void>(car, slot::kSetFlag, 2u, 0u);
                Virtual<std::uint32_t>(car, slot::kSetLeader, static_cast<void*>(last));
                Virtual<std::uint32_t>(car, slot::kPlace, static_cast<const float*>(point),
                                       static_cast<const float*>(&At<float>(last, field::kHeading)));
                Virtual<std::uint32_t>(car, slot::kEnterOccupant, At<void*>(self, train::kOccupant),
                                       At<std::uint32_t>(self, train::kWord), 0u, 1u);
                void* value = car;
                CallThis<void>(addr::SC4PtrVector_PushBack, self + train::kCars, &value);
                Virtual<std::uint32_t>(car, kAddRef);
                if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x10u)
                    Virtual<std::uint32_t>(car, slot::kPushState, 0x10u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
            }
        }
    }
    const std::uint8_t answer = car ? 1 : 0;
    if (car)
        Release(car);
    return answer;
}

// ====================================================================================
// The frame (tr3a)

// An x87 value as the FPU keeps it, its ten bytes: FSIN and FCOS leave 64
// bits of mantissa whatever the precision control, and slot 72 multiplies
// them as they are (as the tank's frame, part ro3b).
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

// A model transform as slots 12 and 75 take it: a count and flags, a 3x3
// matrix, a point and a scale.
struct ModelTransform {
    std::uint8_t count, flags;
    std::uint8_t unused[2];
    float m[9];
    float point[3];
    float scale;
};
static_assert(sizeof(ModelTransform) == 0x38, "slot 12's transform");

void Identity(ModelTransform& s)
{
    static constexpr float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(s.m, kIdentity, sizeof kIdentity);
    s.point[0] = s.point[1] = s.point[2] = 0;
    s.scale = 1;
}

// The matrix turned by `angle` about (0, 0, z), z = `inverse` x 0x00B0A090
// (the x and y of the axis 0x00A81054 times it): the quaternion's x and y
// floats, z unrounded, the cosine at 64 bits; its matrix's nine entries
// floats; each row of the product a float.
void Rock(ModelTransform& t, float angle, double inverse)
{
    const double half = static_cast<double>(angle) * Global<float>(addr::SC4_FloatMinusHalf);
    const double axis = inverse * One();
    const float n0 = static_cast<float>(Zero() * axis);
    const float n1 = n0;
    const float n2 = static_cast<float>(axis);
    X87 sine, cosine;
    X87SinCos(half, &sine, &cosine);
    const float x = static_cast<float>(X87Times(sine, n0));
    const float y = static_cast<float>(X87Times(sine, n1));
    const double z = X87Times(sine, n2);
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    const double yy = static_cast<double>(y) * y;
    const double yy2 = yy + yy;
    const double xx = static_cast<double>(x) * x;
    const double xx2 = xx + xx;
    const double zz = z * z;
    const float zz2 = static_cast<float>(zz + zz);
    const double xx1 = one - xx2;
    const float a = static_cast<float>(xx1 - yy2);
    const float b = static_cast<float>(xx1 - zz2);
    const float c = static_cast<float>((one - yy2) - zz2);
    const double zx = z * x;
    const float xz2 = static_cast<float>(zx + zx);
    const double yx = static_cast<double>(y) * x;
    const float xy2 = static_cast<float>(yx + yx);
    const double xw = X87Times(cosine, x);
    const float xw2 = static_cast<float>(xw + xw);
    const double zy = z * y;
    const float yz2 = static_cast<float>(zy + zy);
    const double yw = X87Times(cosine, y);
    const float yw2 = static_cast<float>(yw + yw);
    const double zw = X87Times(cosine, z);
    const double zw2 = zw + zw;
    const float p = static_cast<float>(xy2 + zw2);
    const float q = static_cast<float>(static_cast<double>(xz2) - yw2);
    const float r = static_cast<float>(xy2 - zw2);
    const float s = static_cast<float>(static_cast<double>(yz2) + xw2);
    const float u = static_cast<float>(static_cast<double>(yw2) + xz2);
    const float v = static_cast<float>(static_cast<double>(yz2) - xw2);
    // The rows (m[6..8], m[3..5], m[0..2]), the sums in the game's order.
    float* const m = t.m;
    auto mul = [](float f, float g) { return static_cast<double>(f) * g; };
    const float e3 = static_cast<float>((mul(s, m[7]) + mul(a, m[8])) + mul(q, m[6]));
    const float e1 = static_cast<float>((mul(u, m[8]) + mul(r, m[7])) + mul(c, m[6]));
    const float e2 = static_cast<float>((mul(v, m[8]) + mul(b, m[7])) + mul(p, m[6]));
    const float g3 = static_cast<float>((mul(s, m[4]) + mul(a, m[5])) + mul(q, m[3]));
    const float g1 = static_cast<float>((mul(u, m[5]) + mul(r, m[4])) + mul(c, m[3]));
    const float g2 = static_cast<float>((mul(v, m[5]) + mul(b, m[4])) + mul(p, m[3]));
    const float h1 = static_cast<float>((mul(m[2], u) + mul(m[1], r)) + mul(m[0], c));
    const float h2 = static_cast<float>((mul(m[2], v) + mul(m[0], p)) + mul(m[1], b));
    const float h3 = static_cast<float>((mul(m[1], s) + mul(m[0], q)) + mul(m[2], a));
    m[0] = h1, m[1] = h2, m[2] = h3;
    m[3] = g1, m[4] = g2, m[5] = g3;
    m[6] = e1, m[7] = e2, m[8] = e3;
}

// 0x00557BF0, the train's slot 72 (word, seconds), ret 8: cSC4Vehicle's
// frame (0x0055BA20). With the point +0x2DC unset (x or z below 0): the last
// car's (the train's without cars) position (slot 7) into it, its occupant
// (slot 137) referenced into +0x2E8 and its +0x1F4 (slot 136) into +0x2EC.
// A car added (slot 199) while fewer than wanted; slot 198 (seconds) out of
// the state 0x14. With the automata flag 0x20 and slot 125 above 0.01
// (0x00A823A4) the timer +0x2F0 gains seconds x 0.001 (0x00A867A4) and from
// 10 on (0x00A9C7BC) Remove(false) when cRZRandomSuper::Next is below
// 0x7FFFFFFF; without the flag it is zeroed. In the state 0x10 (slot 57),
// with +0x2F4 nonzero and below the template entry's +0x4C: the models of
// the train and then of each car rocked, each by its own draw (cRZRandom::
// Range from -A to A, A = +0x2F4 / +0x4C x +0x50 x pi/180): the first
// model's transform (slot 12, from the identity) turned about (0, 0, 1),
// its count up one and its flag 4 set, and every model's set (slot 75). A
// car's transform starts from the identity again (the count up one) when
// the flags are set, as they always are after the train's. Then slot 128.
// AL the base frame's (the rest of EAX slot 128's).
//
// The x87 at 53 bits: the bounds doubles; the train's axis from the
// unrounded 1 / sqrt(1.0), the cars' from its float.
std::uint32_t __fastcall TrainFrame(char* self, void*, std::uint32_t word, float seconds)
{
    static bool logged;
    FirstCall("TrainFrame", logged);
    const std::uint8_t framed = Bool(CallThis<std::uint32_t>(addr::cSC4Vehicle_Frame, self, word, seconds));
    float* const point = &At<float>(self, train::kPoint);
    if (point[0] < Zero() || point[2] < Zero()) {
        char** const cars = &At<char*>(self, train::kCars);
        char* const last = cars[0] == cars[1] ? self : At<char**>(self, train::kCars + 4)[-1];
        Virtual<std::uint32_t>(last, slot::kGetPosition, point);
        void* const occupant = Virtual<void*>(last, slot::kOccupantOf);
        void* const held = At<void*>(self, train::kOccupant);
        if (occupant != held) {
            if (occupant)
                Virtual<std::uint32_t>(occupant, kAddRef);
            At<void*>(self, train::kOccupant) = occupant;
            if (held)
                Release(held);
        }
        At<std::uint32_t>(self, train::kWord) = Virtual<std::uint32_t>(last, slot::kDirectionOf);
    }
    if (At<std::uint32_t>(self, train::kWanted) > CarCount(self))
        Virtual<std::uint32_t>(self, slot::kAddCar);
    if (Virtual<std::uint32_t>(self, slot::kStateType) != 0x14u)
        Virtual<std::uint32_t>(self, slot::kSlot198, seconds);
    if (CheckFlag(self, 0x20u)) {
        if (Virtual<double>(self, slot::kSlot125) > Global<float>(addr::cSC4AutomatonBase_SideShare)) {
            const double timer = static_cast<double>(seconds) * Global<float>(addr::cSC4CitySituationManager_Milli) +
                                 At<float>(self, train::kWord2F0);
            At<float>(self, train::kWord2F0) = static_cast<float>(timer);
            if (timer >= Global<float>(addr::cSC4Train_RemoveAfter) &&
                CallThis<std::uint32_t>(addr::cRZRandomSuper_Next,
                                        reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random)) < 0x7FFFFFFFu)
                Virtual<std::uint32_t>(self, slot::kRemove, 0u);
        }
    } else {
        At<std::uint32_t>(self, train::kWord2F0) = 0;
    }
    char* entry = nullptr;
    if (Virtual<std::uint32_t>(self, slot::kInState, 0x10u) && At<float>(self, train::kWord2F4) != Zero())
        entry = TemplateEntry(self);
    const float rock = At<float>(self, train::kWord2F4);
    if (entry && rock < At<float>(entry, 0x4C)) {
        const double most = static_cast<double>(rock) / At<float>(entry, 0x4C) *
                            (static_cast<double>(At<float>(entry, 0x50)) * Global<float>(addr::cSC4Train_DegreesToRadians));
        const double least = most * Global<float>(addr::cSC4Train_MinusOne);
        void* const generator = reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random);
        const float angle = static_cast<float>(CallThis<double>(addr::cRZRandom_Range, generator, least, most));
        ModelTransform s{};
        Identity(s);
        Virtual<std::uint32_t>(self, slot::kModelTransform, &s);
        const double inverse = Global<float>(addr::SC4_FloatOneA81228) / std::sqrt(Global<double>(addr::SC4_DoubleOne));
        const float inverseF = static_cast<float>(inverse);
        Rock(s, angle, inverse);
        ++s.count;
        s.flags |= 4;
        Virtual<std::uint32_t>(self, slot::kSetModelTransforms, &s);
        ForEachCar(self, [&](char* car) {
            if (!car)
                return;
            const float turn = static_cast<float>(CallThis<double>(addr::cRZRandom_Range, generator, least, most));
            if (s.flags) {
                Identity(s);
                s.flags = 0;
                ++s.count;
            }
            Virtual<std::uint32_t>(car, slot::kModelTransform, &s);
            Rock(s, turn, inverseF);
            ++s.count;
            s.flags |= 4;
            Virtual<std::uint32_t>(car, slot::kSetModelTransforms, &s);
        });
    }
    const std::uint32_t last = Virtual<std::uint32_t>(self, slot::kSlot128);
    return (last & 0xFFFFFF00u) | framed;
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
            log::Write("automata trains %s: no address entry for 0x%08X", what,
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
    log::Write("automata trains %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Part tr1.
const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4TrainCar_Create, CarCreate),
    OPENSC4_DETOUR(addr::cSC4Train_Create, TrainCreate),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_Create, MassTransitCreate),
    OPENSC4_DETOUR(addr::cSC4TrainCar_Construct, CarConstruct),
    OPENSC4_DETOUR(addr::cSC4Train_Construct, TrainConstruct),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_Construct, MassTransitConstruct),
    OPENSC4_DETOUR(addr::cSC4TrainCar_PoolAllocate, CarPoolAllocate),
    OPENSC4_DETOUR(addr::cSC4TrainCar_PoolFree, CarPoolFree),
    OPENSC4_DETOUR(addr::cSC4TrainCar_DeletingDestructor, CarDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4Train_Destroy, TrainDestroy),
    OPENSC4_DETOUR(addr::cSC4Train_DeletingDestructor, TrainDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4TrainCar_QueryInterface, CarQueryInterface),
    OPENSC4_DETOUR(addr::cSC4Train_QueryInterface, TrainQueryInterface),
    OPENSC4_DETOUR(addr::cSC4TrainCar_GetGZCLSID, CarGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Train_GetGZCLSID, TrainGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_GetGZCLSID, MassTransitGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Train_Write, TrainWrite),
    OPENSC4_DETOUR(addr::cSC4Train_Read, TrainRead),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_Write, MassTransitWrite),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_Read, MassTransitRead),
    OPENSC4_DETOUR(addr::cSC4TrainCar_Init, CarInit),
    OPENSC4_DETOUR(addr::cSC4Train_Init, TrainInit),
    OPENSC4_DETOUR(addr::cSC4Train_Shutdown, TrainShutdown),
    OPENSC4_DETOUR(addr::cSC4TrainCar_GetTrain, CarGetTrain),
    OPENSC4_DETOUR(addr::cSC4TrainCar_Collide, CarCollide),
    OPENSC4_DETOUR(addr::cSC4TrainCar_LightFrame, CarLightFrame),
    OPENSC4_DETOUR(addr::cSC4Train_Slot175, TrainSlot175),
    OPENSC4_DETOUR(addr::cSC4Train_TrimCars, TrainTrimCars),
    OPENSC4_DETOUR(addr::cSC4Train_CarCount, TrainCarCount),
    OPENSC4_DETOUR(addr::cSC4Train_SetCarCount, TrainSetCarCount),
    OPENSC4_DETOUR(addr::cSC4Train_WantedCars, TrainWantedCars),
};

// Part tr2a.
const Detour kFrame[] = {
    OPENSC4_DETOUR(addr::cSC4TrainCar_EnterState, CarEnterState),
    OPENSC4_DETOUR(addr::cSC4Train_EnterState, TrainEnterState),
    OPENSC4_DETOUR(addr::cSC4Train_LeaveState, TrainLeaveState),
    OPENSC4_DETOUR(addr::cSC4Train_CanMeet, TrainCanMeet),
    OPENSC4_DETOUR(addr::cSC4Train_Steer, TrainSteer),
    OPENSC4_DETOUR(addr::cSC4Train_SpeedFactor, TrainSpeedFactor),
    OPENSC4_DETOUR(addr::cSC4Train_Slot172, TrainSlot172),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_Slot172, MassTransitSlot172),
    OPENSC4_DETOUR(addr::cSC4Train_NextStep, TrainNextStep),
    OPENSC4_DETOUR(addr::cSC4MassTransitTrain_NextStep, MassTransitNextStep),
    OPENSC4_DETOUR(addr::cSC4Train_Collide, TrainCollide),
    OPENSC4_DETOUR(addr::cSC4TrainCar_EnterOccupant, CarEnterOccupant),
    OPENSC4_DETOUR(addr::cSC4Train_EnterOccupant, TrainEnterOccupant),
};

// Part tr2b.
const Detour kMove[] = {
    OPENSC4_DETOUR(addr::cSC4Train_MoveOnPath, TrainMoveOnPath),
    OPENSC4_DETOUR(addr::cSC4Train_PathTo, TrainPathTo),
    OPENSC4_DETOUR(addr::cSC4Train_PlaceOnNetwork, TrainPlaceOnNetwork),
    OPENSC4_DETOUR(addr::cSC4Train_NextOccupant, TrainNextOccupant),
    OPENSC4_DETOUR(addr::cSC4Train_AddCar, TrainAddCar),
};

// Part tr3a.
const Detour kTrainFrame[] = {
    OPENSC4_DETOUR(addr::cSC4Train_Frame, TrainFrame),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kFrame, sizeof kFrame / sizeof kFrame[0], "frame") &&
           InstallList(kMove, sizeof kMove / sizeof kMove[0], "move") &&
           InstallList(kTrainFrame, sizeof kTrainFrame / sizeof kTrainFrame[0], "train frame");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.trains", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Train_Construct,
                        "Our trains (the train car, the train and the mass transit train), the objects: the three "
                        "factories and constructors, the car's pool, the car's and the train's deleting destructors, "
                        "the train's destructor body, the two QueryInterfaces and the three GetGZCLSIDs; the train's "
                        "and the mass transit train's Writes and Reads; the car's and the train's Inits and the "
                        "train's Shutdown; the car's slots 50, 65 and 120 and the train's 175 and 193 to 196 (31 "
                        "detours, part tr1); the frames: the car's slots 110 and 132, the train's 65, 73, 110, 111, "
                        "121, 125, 132, 165 and 172, the mass transit train's 165 and 172 (13 detours, part tr2a); "
                        "the movement: the train's slots 129, 131, 134, 197 and 199 (5 detours, part tr2b); the "
                        "train's frame, slot 72 (1 detour, part tr3a; src/core/automata_trains.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_trains
