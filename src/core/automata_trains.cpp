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
//   simulators.automata.trains: the 31 detours in kObject (tr1) below.
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

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.trains", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Train_Construct,
                        "Our trains (the train car, the train and the mass transit train), the objects: the three "
                        "factories and constructors, the car's pool, the car's and the train's deleting destructors, "
                        "the train's destructor body, the two QueryInterfaces and the three GetGZCLSIDs; the train's "
                        "and the mass transit train's Writes and Reads; the car's and the train's Inits and the "
                        "train's Shutdown; the car's slots 50, 65 and 120 and the train's 175 and 193 to 196 (31 "
                        "detours, part tr1; src/core/automata_trains.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_trains
