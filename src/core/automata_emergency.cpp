// Our emergency vehicles, parts em1 to em4 (build 641): a base on cSC4Vehicle
// (src/core/automata_vehicle.cpp) and cSC4FireTruck, cSC4PatrolCar and
// cSC4Ambulance on it. em1: the objects (the three factories, the four
// constructors, the base's and the fire truck's destructor bodies, the three
// deleting destructors and the three GetGZCLSIDs), the save (the four Writes
// and Reads), the life cycle (the base's Init, the base's, the fire truck's
// and the patrol car's Shutdowns) and the queries (the base's slots 23, 24,
// 26, 29, 60, 105, 135 and 143, the ambulance's 192 and 193). em2a: the
// base's states and mission (slots 25, 73 with its deque count, 79, 93, 109
// to 111, 146 and 194). em2b: the base's frames (slots 72, 195, 196 and
// 198, 0x0052F870 and 0x0052F910). em3a: the fire truck's slots 25, 60,
// 111, 121, 165, 193, 197 and 199, the crew sent and recalled (0x00532C40,
// 0x00532D30), the counts near it (0x00532AE0, 0x00533410) and the route
// step helpers (0x00533030, 0x00533290, which the train's 165 also calls).
// em3b: the patrol car's slots 25, 109, 110, 111, 193, 197, 199 and 200 and
// its riot police sent and recalled (0x00543C70, 0x00543D60). em4: the
// base's slot 80 (the goal) and the route extended from its last cell to the
// goal's nearest occupant (0x00532660); the route's deque code (24 template
// functions) stays the game's. The layout is in
// src/core/automata_emergency.h and the survey in docs/automata.md, "Part
// em". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.emergency: the 37 detours in kObject (em1), the 10
//   in kStates (em2a), the 6 in kFrames (em2b), the 14 in kFireTruck (em3a),
//   the 10 in kPatrolCar (em3b) and the 2 in kRoute (em4) below.
//
// What it does. Each class is a block of SC4::operator_new (0x2E4, 0x2DC and
// 0x2D8 bytes) and goes back through SC4::operator_delete. The base's
// constructor runs cSC4Vehicle's with four zero words, sets its six vtables,
// its point to three copies of the float 0x00B22814 and its other words to
// zero; each class's runs the base's and sets its own six. The fire truck
// makes two occupant filters (12 bytes each, SC4::OccupantFilterBase with
// the vtables 0x00A949B8 and 0x00A985B0) into +0x2D8 and +0x2DC.
//
// Oddities kept: the base's constructor zeroes +0x2D0 and then tests it to
// release it (dead); the base's destructor body releases the siren and
// +0x2D0 but not +0x2C4 (Shutdown does), and the fire truck's inlines it;
// +0x2D0 is released through its virtual base (+4's table, second word);
// the base's Read reads +0x2C4 without releasing what was there, and the
// mission through a local that holds the part's address when the read fails;
// the fire truck's and the patrol car's Reads hold the reference they read
// once more (AddRef); the Reads read their version into the stream
// argument's own stack slot (what is there when the read fails is the
// address's low word); the fire truck's and the patrol car's Shutdowns do not
// test the city or its slot 44 object for null; slot 60 gives slot 194 the
// word +0x2D0 with its low byte replaced by whether it is null; slot 105
// answers the last call's EAX when +0x98 is null. AL only in the game (ours
// zero-extends): Init, the Writes and Reads, slot 60; AX only: slot 23.
//
// Floats: Init's speed is the word 0x00B21CBC read unsigned (FILD, plus 2^32
// when negative) times the float 0x00A98094, at 53 bits, stored as a float;
// the ambulance's slot 193 answers the int 0x00B09934 as FILD leaves it
// (ours a double, exact).
//
// Oddities kept (em4): slot 80 releases the old target before it holds the
// new one (the same object given again with one reference is freed there);
// 0x00532660 never releases the path finder 0x00B43DA8 gave it, reads the
// occupants' cells unsigned and compares the unrounded squared distance with
// the float it stored.
//
// Still the game's, at their addresses: cSC4Vehicle's code (part vb), the
// base's slot 197 (a fold), the core's slots 80 0x00505860 and 105 0x005096B0 and
// cSC4Vehicle's slot 60 0x0055B6C0, cSC4AutomatonBase::RemoveNow, the jumps
// and adjustor thunks the vtables hold, the occupant filters' code, the
// stream helpers and the sound service; the fire crew's and the riot police's
// code (the pedestrians').

#include "core/automata_emergency.h"

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

namespace opensc4::automata_emergency {
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
    log::Write("automata emergency: first %s", name);
}

constexpr std::size_t kAddRef = 0x04, kRelease = 0x08;
constexpr std::size_t kVtable4 = 0x04, kSerializable = 0x08;
constexpr std::size_t kDraw = 0x80, kObject98 = 0x98; // cSC4Vehicle's fields this part writes
constexpr std::uint32_t kStopSound = 0x6A451908;     // the sound service's message that stops a source
constexpr std::uint32_t kPlaySound = 0xEA451903;     // and the one that plays one (key, source, 0, 0)
// cSC4Vehicle's fields em2a reads.
constexpr std::size_t kPosition = 0x40, kStateStack = 0xB4, kDeque204 = 0x204, kRouteStart = 0x22C, kRouteFinish = 0x23C;
constexpr std::size_t kNext = 0x54; // a node's next, in 0x00B43D54's cell lists
// em4's: a route (SC4RouteDeque, 3-byte cells, 0x7E-byte blocks).
constexpr std::size_t kRouteSize = 0x28, kRouteBlock = 0x7E;
// em2b's.
constexpr std::size_t kObjectB0 = 0xB0, kSmoothed1B0 = 0x1B0, kOccupant = 0x1EC;
constexpr std::uint32_t kMissionMessage = 0xCA8DCDB4; // 0x0052F910 posts it
constexpr std::uint32_t kMoveSound = 0xCA45190F;      // the sound service's: a source moved (source, x, y, z)

std::uint32_t Release(void* object) { return Virtual<std::uint32_t>(object, kRelease); }

// An object held through its virtual base: +4 its table, whose second word
// leads from +4 to the part with the reference count.
void ReleaseThroughBase(char* held)
{
    char* const part = held + 4 + At<const std::int32_t*>(held, 4)[1];
    Release(part);
}

// A field holding a reference given `value`: held (AddRef) when it differs,
// the old one released after the store.
void Assign(char* self, std::size_t at, void* value)
{
    void* const old = At<void*>(self, at);
    if (value == old)
        return;
    if (value)
        Virtual<std::uint32_t>(value, kAddRef);
    At<void*>(self, at) = value;
    if (old)
        Release(old);
}

// A field holding a reference cleared, then released.
void ClearAndRelease(char* self, std::size_t at)
{
    if (void* const held = At<void*>(self, at)) {
        At<void*>(self, at) = nullptr;
        Release(held);
    }
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

// The version word of a Read that reads it into the stream argument's own
// stack slot: what is there when the read fails is the address's low word.
std::uint16_t VersionInArgument(void* s)
{
    std::uint16_t version = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(s));
    StreamCall(s, stream::kUint16, &version);
    return version;
}

bool CheckFlag(char* self, std::uint32_t flag) { return Virtual<std::uint8_t>(self, slot::kCheckFlag, flag) != 0; }

// ====================================================================================
// The objects

constexpr std::uintptr_t kBaseTables[6] = {
    addr::cSC4EmergencyVehicle_Vtable,  addr::cSC4EmergencyVehicle_Vtable4,  addr::cSC4EmergencyVehicle_Vtable8,
    addr::cSC4EmergencyVehicle_VtableC, addr::cSC4EmergencyVehicle_Vtable20, addr::cSC4EmergencyVehicle_Vtable38};
constexpr std::uintptr_t kFireTables[6] = {addr::cSC4FireTruck_Vtable,   addr::cSC4FireTruck_Vtable4,
                                           addr::cSC4FireTruck_Vtable8,  addr::cSC4FireTruck_VtableC,
                                           addr::cSC4FireTruck_Vtable20, addr::cSC4FireTruck_Vtable38};
constexpr std::uintptr_t kPatrolTables[6] = {addr::cSC4PatrolCar_Vtable,   addr::cSC4PatrolCar_Vtable4,
                                             addr::cSC4PatrolCar_Vtable8,  addr::cSC4PatrolCar_VtableC,
                                             addr::cSC4PatrolCar_Vtable20, addr::cSC4PatrolCar_Vtable38};
constexpr std::uintptr_t kAmbulanceTables[6] = {addr::cSC4Ambulance_Vtable,   addr::cSC4Ambulance_Vtable4,
                                                addr::cSC4Ambulance_Vtable8,  addr::cSC4Ambulance_VtableC,
                                                addr::cSC4Ambulance_Vtable20, addr::cSC4Ambulance_Vtable38};

// The GZCOM factories (cdecl): a block of SC4::operator_new and the class's
// constructor (a tail jump); null when the block is.
void* Create(std::size_t size, std::uintptr_t construct)
{
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, size);
    if (!block)
        return nullptr;
    return CallThis<char*>(construct, block);
}

// 0x00514660, the fire truck's.
void* __cdecl FireTruckCreate()
{
    static bool logged;
    FirstCall("FireTruckCreate", logged);
    return Create(kFireTruckSize, addr::cSC4FireTruck_Construct);
}

// 0x00514680, the patrol car's.
void* __cdecl PatrolCarCreate()
{
    static bool logged;
    FirstCall("PatrolCarCreate", logged);
    return Create(kPatrolCarSize, addr::cSC4PatrolCar_Construct);
}

// 0x00514740, the ambulance's.
void* __cdecl AmbulanceCreate()
{
    static bool logged;
    FirstCall("AmbulanceCreate", logged);
    return Create(kAmbulanceSize, addr::cSC4Ambulance_Construct);
}

// 0x00532190, the base's constructor: cSC4Vehicle's with four zero words,
// the point three copies of the float 0x00B22814, the rest zero.
char* __fastcall BaseConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("BaseConstruct", logged);
    CallThis<char*>(addr::cSC4Vehicle_Construct, self, 0u, 0u, 0u, 0u);
    SetVtables(self, kBaseTables);
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, base::kPoint + i) = Global<std::uint32_t>(addr::cSC4EmergencyVehicle_DefaultFloat);
    At<std::uint32_t>(self, base::kHeld) = 0;
    At<std::uint32_t>(self, base::kSiren) = 0;
    At<std::uint32_t>(self, base::kMission) = 0;
    At<std::uint8_t>(self, base::kByte) = 0;
    At<std::uint32_t>(self, base::kTarget) = 0;
    At<std::uint16_t>(self, base::kWord) = 0;
    // The game then releases +0x2D0 when it is set: it was just zeroed.
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

// 0x005337E0, the fire truck's constructor: the base's, +0x2D8 to +0x2E0
// zero, then the two filters held into +0x2D8 and +0x2DC.
char* __fastcall FireTruckConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("FireTruckConstruct", logged);
    CallThis<char*>(addr::cSC4EmergencyVehicle_Construct, self);
    SetVtables(self, kFireTables);
    At<std::uint32_t>(self, fire::kFilter) = 0;
    At<std::uint32_t>(self, fire::kFilter2) = 0;
    At<std::uint32_t>(self, fire::kCrew) = 0;
    Assign(self, fire::kFilter, MakeFilter(addr::SC4OccupantFilter43_vftable, addr::SC4OccupantFilter43_vftable2));
    Assign(self, fire::kFilter2, MakeFilter(addr::cSC4FireTruck_FilterVtable, addr::cSC4FireTruck_FilterVtable2));
    return self;
}

// 0x00544240, the patrol car's constructor: the base's, +0x2D8 zero.
char* __fastcall PatrolCarConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("PatrolCarConstruct", logged);
    CallThis<char*>(addr::cSC4EmergencyVehicle_Construct, self);
    SetVtables(self, kPatrolTables);
    At<std::uint32_t>(self, patrol::kCrew) = 0;
    return self;
}

// 0x00504F30, the ambulance's constructor: the base's.
char* __fastcall AmbulanceConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("AmbulanceConstruct", logged);
    CallThis<char*>(addr::cSC4EmergencyVehicle_Construct, self);
    SetVtables(self, kAmbulanceTables);
    return self;
}

// The base's +0x2D4 and +0x2D0 released (not cleared).
void ReleaseBaseFields(char* self)
{
    if (void* const siren = At<void*>(self, base::kSiren))
        Release(siren);
    if (char* const held = At<char*>(self, base::kHeld))
        ReleaseThroughBase(held);
}

// 0x00504EF0, the base's destructor body: the siren and +0x2D0 released,
// then cSC4Vehicle's (a tail jump).
void* __fastcall BaseDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("BaseDestroy", logged);
    ReleaseBaseFields(self);
    return CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
}

// 0x00532270, the base's and the ambulance's deleting destructor (flags),
// ret 4: SC4::operator_delete, no null test.
char* __fastcall BaseDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("BaseDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4EmergencyVehicle_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x00533950, the fire truck's destructor body: +0x2E0, +0x2DC and +0x2D8
// released (not cleared), then the base's fields and cSC4Vehicle's (the
// base's body inlined).
void* __fastcall FireTruckDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("FireTruckDestroy", logged);
    for (const std::size_t at : {fire::kCrew, fire::kFilter2, fire::kFilter})
        if (void* const held = At<void*>(self, at))
            Release(held);
    ReleaseBaseFields(self);
    return CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
}

// 0x00533930, the fire truck's deleting destructor (flags), ret 4.
char* __fastcall FireTruckDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("FireTruckDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4FireTruck_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x005442D0, the patrol car's deleting destructor (flags), ret 4: +0x2D8
// released (not cleared), then the base's body.
char* __fastcall PatrolCarDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("PatrolCarDeletingDestructor", logged);
    if (void* const crew = At<void*>(self, patrol::kCrew))
        Release(crew);
    CallThis<void*>(addr::cSC4EmergencyVehicle_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The serializable part's slot 5 of each class: its class ID.

// 0x005338E0, the fire truck's.
std::uint32_t __fastcall FireTruckGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("FireTruckGetGZCLSID", logged);
    return kFireTruckClassId;
}

// 0x00544280, the patrol car's.
std::uint32_t __fastcall PatrolCarGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("PatrolCarGetGZCLSID", logged);
    return kPatrolCarClassId;
}

// 0x00505060, the ambulance's.
std::uint32_t __fastcall AmbulanceGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("AmbulanceGetGZCLSID", logged);
    return kAmbulanceClassId;
}

// ====================================================================================
// The save (this = the object + 8, the serializable part)

// 0x0052FA60, the base's +8 slot 3 (stream), ret 4: version 2 (uint16),
// cSC4Vehicle's Write; when true the point (SC4Stream::WriteVector3), +0x2C4
// (a reference), the byte, the word and the mission. AL cSC4Vehicle's.
std::uint32_t __fastcall BaseWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("BaseWrite", logged);
    char* const self = part - kSerializable;
    StreamCall(s, stream::kUint16, 2u);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Write, part, s);
    if (!ok)
        return ok;
    CallCdecl<void*>(addr::SC4Stream_WriteVector3, s, self + base::kPoint);
    CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(self, base::kTarget));
    StreamCall(s, stream::kUint8, static_cast<std::uint32_t>(At<std::uint8_t>(self, base::kByte)));
    StreamCall(s, stream::kUint16, static_cast<std::uint32_t>(At<std::uint16_t>(self, base::kWord)));
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, base::kMission));
    return ok;
}

// 0x0052FAE0, the base's +8 slot 4: AL 0 unless the version (into the stream
// argument's slot) is 2; cSC4Vehicle's Read; when true the point, +0x2C4 read
// as 0x26BB767E (what was there not released), the byte, the word and the
// mission (through a local holding the part's address). AL cSC4Vehicle's.
std::uint32_t __fastcall BaseRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("BaseRead", logged);
    char* const self = part - kSerializable;
    if (VersionInArgument(s) != 2)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Read, part, s);
    if (!ok)
        return ok;
    CallCdecl<void*>(addr::SC4Stream_ReadVector3, s, self + base::kPoint);
    CallCdecl<void*>(addr::SC4Stream_ReadReference, s, kTargetIid, self + base::kTarget, 0u);
    StreamCall(s, stream::kUint8, &At<std::uint8_t>(self, base::kByte));
    StreamCall(s, stream::kUint16, &At<std::uint16_t>(self, base::kWord));
    std::uint32_t mission = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(part));
    StreamCall(s, stream::kUint32, &mission);
    At<std::uint32_t>(self, base::kMission) = mission;
    return ok;
}

// The fire truck's and the patrol car's Write (version 2, the base's Write,
// the crew reference) and Read (the version into the argument's slot, the
// base's Read, the crew read as 0xA9B40F05 into a zeroed local and held).
std::uint32_t CrewWrite(char* part, void* s, std::size_t crew)
{
    StreamCall(s, stream::kUint16, 2u);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4EmergencyVehicle_Write, part, s);
    if (ok)
        CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(part - kSerializable, crew));
    return ok;
}

std::uint32_t CrewRead(char* part, void* s, std::size_t crew)
{
    if (VersionInArgument(s) != 2)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4EmergencyVehicle_Read, part, s);
    if (ok) {
        void* value = nullptr;
        CallCdecl<void*>(addr::SC4Stream_ReadReference, s, kCrewIid, static_cast<void*>(&value), 0u);
        Assign(part - kSerializable, crew, value);
    }
    return ok;
}

// 0x00532EE0, the fire truck's +8 slot 3: +0x2E0.
std::uint32_t __fastcall FireTruckWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("FireTruckWrite", logged);
    return CrewWrite(part, s, fire::kCrew);
}

// 0x00532F20, the fire truck's +8 slot 4.
std::uint32_t __fastcall FireTruckRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("FireTruckRead", logged);
    return CrewRead(part, s, fire::kCrew);
}

// 0x00543DB0, the patrol car's +8 slot 3: +0x2D8.
std::uint32_t __fastcall PatrolCarWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("PatrolCarWrite", logged);
    return CrewWrite(part, s, patrol::kCrew);
}

// 0x00543DF0, the patrol car's +8 slot 4.
std::uint32_t __fastcall PatrolCarRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("PatrolCarRead", logged);
    return CrewRead(part, s, patrol::kCrew);
}

// 0x00504BF0, the ambulance's +8 slot 3: version 1, then the base's Write
// (its EAX).
std::uint32_t __fastcall AmbulanceWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("AmbulanceWrite", logged);
    StreamCall(s, stream::kUint16, 1u);
    return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_Write, part, s);
}

// 0x00504C10, the ambulance's +8 slot 4: AL 0 unless the version (into the
// stream argument's slot) is 1; else the base's Read (its EAX).
std::uint32_t __fastcall AmbulanceRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("AmbulanceRead", logged);
    if (VersionInArgument(s) != 1)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_Read, part, s);
}

// ====================================================================================
// The life cycle

// 0x0052F130, the base's slot 3 (Init): AL 1 at once when initialised (the
// occupant part's slot 5); AL 0 when cSC4Vehicle's Init is false; with the
// automata flag 0x80 the siren on (slot 194) and the speed (the word
// 0x00B21CBC unsigned times the float 0x00A98094) into +0x80 and slot 36
// with the rate 1. AL 1.
std::uint32_t __fastcall BaseInit(char* self, void*)
{
    static bool logged;
    FirstCall("BaseInit", logged);
    if (Virtual<std::uint8_t>(self + kVtable4, slot::kInitialized))
        return 1;
    if (!CallThis<std::uint8_t>(addr::cSC4Vehicle_Init, self))
        return 0;
    if (CheckFlag(self, 0x80u)) {
        Virtual<void>(self, slot::kSetSiren, 1u);
        const float speed =
            static_cast<float>(static_cast<double>(Global<std::uint32_t>(addr::cSC4EmergencyVehicle_SpeedWord)) *
                               static_cast<double>(Global<float>(addr::cSC4EmergencyVehicle_SpeedScale)));
        At<float>(self, kDraw) = speed;
        Virtual<void>(self, slot::kSetSpeed, speed, 1.0f);
    }
    return 1;
}

// 0x0052F380, the base's slot 4 (Shutdown; the ambulance's): +0x2C4
// released and cleared, +0x2D0 cleared and released through its virtual
// base, the sound service 0x00B43CB8 told to stop this source; then
// cSC4Vehicle's Shutdown (a tail jump).
std::uint32_t __fastcall BaseShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("BaseShutdown", logged);
    if (void* const target = At<void*>(self, base::kTarget)) {
        Release(target);
        At<void*>(self, base::kTarget) = nullptr;
    }
    if (char* const held = At<char*>(self, base::kHeld)) {
        At<char*>(self, base::kHeld) = nullptr;
        ReleaseThroughBase(held);
    }
    if (void* const sound = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<std::uint32_t>(sound, slot::kSoundStop, kStopSound, static_cast<void*>(self), 0u, 0u, 0u);
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Shutdown, self);
}

// The fire truck's and the patrol car's: with the crew the city's slot 44
// object's slot 8 (crew, 0), no null tests; the crew cleared and released.
void CrewShutdown(char* self, std::size_t crew)
{
    if (!At<void*>(self, crew))
        return;
    void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
    Virtual<std::uint32_t>(service, slot::kCrewDone, At<void*>(self, crew), 0u);
    ClearAndRelease(self, crew);
}

// 0x00532FB0, the fire truck's slot 4: the crew, then the two filters cleared
// and released; the base's Shutdown (a tail jump).
std::uint32_t __fastcall FireTruckShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("FireTruckShutdown", logged);
    CrewShutdown(self, fire::kCrew);
    ClearAndRelease(self, fire::kFilter);
    ClearAndRelease(self, fire::kFilter2);
    return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_Shutdown, self);
}

// 0x00543EC0, the patrol car's slot 4: the crew; the base's Shutdown.
std::uint32_t __fastcall PatrolCarShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("PatrolCarShutdown", logged);
    CrewShutdown(self, patrol::kCrew);
    return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_Shutdown, self);
}

// ====================================================================================
// The queries

// 0x00505030, the base's slot 23: the word +0x2CA.
std::uint32_t __fastcall BaseWord(char* self, void*)
{
    static bool logged;
    FirstCall("BaseWord", logged);
    return At<std::uint16_t>(self, base::kWord);
}

// 0x00505040, the base's slot 24 (word), ret 4.
void __fastcall BaseSetWord(char* self, void*, std::uint32_t word)
{
    static bool logged;
    FirstCall("BaseSetWord", logged);
    At<std::uint16_t>(self, base::kWord) = static_cast<std::uint16_t>(word);
}

// 0x00505020, the base's slot 26: the mission.
std::uint32_t __fastcall BaseMission(char* self, void*)
{
    static bool logged;
    FirstCall("BaseMission", logged);
    return At<std::uint32_t>(self, base::kMission);
}

// 0x00505050, the base's slot 143: the point's address.
char* __fastcall BasePoint(char* self, void*)
{
    static bool logged;
    FirstCall("BasePoint", logged);
    return self + base::kPoint;
}

// 0x00505070, the ambulance's slot 192: 7.
std::uint32_t __fastcall AmbulanceSlot192(char*, void*)
{
    static bool logged;
    FirstCall("AmbulanceSlot192", logged);
    return 7;
}

// 0x00504BE0, the ambulance's slot 193: the int 0x00B09934 in ST0.
double __fastcall AmbulanceSlot193(char*, void*)
{
    static bool logged;
    FirstCall("AmbulanceSlot193", logged);
    return static_cast<double>(Global<std::int32_t>(addr::cSC4Ambulance_Slot193Int));
}

// 0x0052F300, the base's slot 135: with the automata flag 0x80 slot 79, else
// cSC4AutomatonBase::RemoveNow (both tail jumps).
std::uint32_t __fastcall BaseSlot135(char* self, void*)
{
    static bool logged;
    FirstCall("BaseSlot135", logged);
    if (CheckFlag(self, 0x80u))
        return Virtual<std::uint32_t>(self, slot::kSlot79);
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_RemoveNow, self);
}

// 0x0052F330, the base's slot 29: unless the mission is 6 or a state 0x10 is
// stacked, the mission 6 (slot 25) and the state 0x67 pushed.
void __fastcall BaseGoHome(char* self, void*)
{
    static bool logged;
    FirstCall("BaseGoHome", logged);
    if (Virtual<std::uint32_t>(self, slot::kMission) == 6)
        return;
    if (Virtual<void*>(self, slot::kInState, 0x10u))
        return;
    Virtual<void>(self, slot::kSetMission, 6u);
    Virtual<void>(self, slot::kPushState, 0x67u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
}

// 0x0052F730, the base's slot 105: unless the automata flag 0x80 or the
// state type 0x34, the core's 0x005096B0 (a tail jump); else +0x98 cleared
// and released (its Release's EAX; the last call's when it is null).
std::uint32_t __fastcall BaseSlot105(char* self, void*)
{
    static bool logged;
    FirstCall("BaseSlot105", logged);
    std::uint32_t last = Virtual<std::uint32_t>(self, slot::kCheckFlag, 0x80u);
    if (!(last & 0xFF)) {
        last = Virtual<std::uint32_t>(self, slot::kStateType);
        if (last != 0x34)
            return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Slot105, self);
    }
    void* const held = At<void*>(self, kObject98);
    if (!held)
        return last;
    At<void*>(self, kObject98) = nullptr;
    return Release(held);
}

// 0x0052F780, the base's slot 60 (event, flag), ret 8: the event 9 with the
// flag's low byte clear sets the siren by whether +0x2D0 is null (the word
// with its low byte replaced) and answers AL 1; else cSC4Vehicle's slot 60
// (a tail jump).
std::uint32_t __fastcall BaseOnEvent(char* self, void*, std::uint32_t event, std::uint32_t flag)
{
    static bool logged;
    FirstCall("BaseOnEvent", logged);
    if (event != 9 || (flag & 0xFF))
        return CallThis<std::uint32_t>(addr::cSC4Vehicle_Slot60, self, event, flag);
    const std::uint32_t held = At<std::uint32_t>(self, base::kHeld);
    Virtual<void>(self, slot::kSetSiren, (held & 0xFFFFFF00u) | (held == 0 ? 1u : 0u));
    return 1;
}

// ====================================================================================
// The states and the mission (em2a)

// A string of the game's (cRZBaseString's layout, as cSC4Vehicle's debug text
// uses it).
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
    char* const buffer = CallCdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
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

// The sound service 0x00B43CB8 told to stop this source, when there is one.
void StopSound(char* self)
{
    if (void* const sound = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<std::uint32_t>(sound, slot::kSoundStop, kStopSound, static_cast<void*>(self), 0u, 0u, 0u);
}

void SetFlag(char* self, std::uint32_t flag, std::uint32_t on) { Virtual<std::uint32_t>(self, slot::kSetFlag, flag, on); }

// 0x0052F1C0, the base's slot 111 (state), ret 4: leaving the state 0x10 or
// 0x34 turns the siren off (slot 194); then the automaton base's
// (0x005535D0), its EAX.
std::uint32_t __fastcall BaseLeaveState(char* self, void*, char* left)
{
    static bool logged;
    FirstCall("BaseLeaveState", logged);
    if (left) {
        const std::uint32_t type = At<std::uint32_t>(left, state::kType);
        if (type == 0x10 || type == 0x34)
            Virtual<std::uint32_t>(self, slot::kSetSiren, 0u);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_LeaveState, self, left);
}

// 0x0052F210, the base's slot 79: without the automata flag 0x80 the core's
// (0x00505820, a tail jump). With it: the mission 1 nothing (EAX 0); a
// mission other than 3 the core's; the mission 3 slot 102 (5, 0), then at
// the point (slot 146) the flag 4 off, the byte +0x2C8 zero, slot 33 (1, 0),
// the state 0x66 set (slot 51), the flag 0x4000 on and the mission 5 (slot
// 25, its EAX); away from it the mission 1, the byte zero, the state 0x65
// pushed and the state 7 for 0x00B21CB4 x 1000 (its EAX).
std::uint32_t __fastcall BaseBlocked(char* self, void*)
{
    static bool logged;
    FirstCall("BaseBlocked", logged);
    if (!CheckFlag(self, 0x80u))
        return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Slot79, self);
    const std::uint32_t mission = At<std::uint32_t>(self, base::kMission);
    if (mission == 1)
        return 0;
    if (mission != 3)
        return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Slot79, self);
    Virtual<std::uint32_t>(self, slot::kSlot102, 5u, 0u);
    if (Virtual<std::uint8_t>(self, slot::kAtPoint)) {
        SetFlag(self, 4u, 0u);
        At<std::uint8_t>(self, base::kByte) = 0;
        Virtual<std::uint32_t>(self, slot::kFade, 1.0f, 0u);
        Virtual<std::uint32_t>(self, slot::kSetState, 0x66u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
        SetFlag(self, 0x4000u, 1u);
        return Virtual<std::uint32_t>(self, slot::kSetMission, 5u);
    }
    Virtual<std::uint32_t>(self, slot::kSetMission, 1u);
    At<std::uint8_t>(self, base::kByte) = 0;
    Virtual<std::uint32_t>(self, slot::kPushState, 0x65u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
    // FLD, FMUL (53 bits), FSTP float.
    const float wait = static_cast<float>(static_cast<double>(Global<float>(addr::cSC4EmergencyVehicle_RetryWait)) *
                                          static_cast<double>(Global<float>(addr::cSC4AutomatonBase_Thousand)));
    return Virtual<std::uint32_t>(self, slot::kPushState, 7u, 0u, wait, 0u, 0u, 0u, 0u);
}

// 0x0052F490, the base's slot 109 (frame seconds), ret 4: AL 0 with no
// states (+0xB4 = +0xB8) or no current state (slot 56); the states 0x64 to
// 0x67 go to slots 195 to 198 (their AL); the rest to cSC4Vehicle's
// (0x0055CA90): its AL when set, else whether the current state changed.
std::uint32_t __fastcall BaseStateFrame(char* self, void*, std::uint32_t seconds)
{
    static bool logged;
    FirstCall("BaseStateFrame", logged);
    if (At<char*>(self, kStateStack) == At<char*>(self, kStateStack + 4))
        return 0;
    char* const current = Virtual<char*>(self, slot::kCurrentState);
    if (!current)
        return 0;
    switch (At<std::uint32_t>(current, state::kType)) {
    case 0x64:
        return Virtual<std::uint8_t>(self, slot::kSlot195, seconds);
    case 0x65:
        return Virtual<std::uint8_t>(self, slot::kSlot196, seconds);
    case 0x66:
        return Virtual<std::uint8_t>(self, slot::kSlot197, seconds);
    case 0x67:
        return Virtual<std::uint8_t>(self, slot::kSlot198, seconds);
    default:
        break;
    }
    const std::uint8_t done = CallThis<std::uint8_t>(addr::cSC4Vehicle_StateFrame, self, seconds);
    if (done)
        return done;
    return Virtual<char*>(self, slot::kCurrentState) != current ? 1 : 0;
}

// 0x0052F7C0, the base's slot 110 (state), ret 4: the state 0x10:
// cSC4Vehicle's (0x0055B6D0), the mission 3 (stored), slot 102 (7, slot 26),
// the siren off, the sound stopped; AL 1. 0x34: the siren on, then
// cSC4Vehicle's (its EAX). 0x67: slot 33 (0, the float 0x00B21C84), then
// cSC4Vehicle's. Others (and null, which cSC4Vehicle's reads): cSC4Vehicle's.
std::uint32_t __fastcall BaseEnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("BaseEnterState", logged);
    if (entered) {
        const std::uint32_t type = At<std::uint32_t>(entered, state::kType);
        if (type == 0x10) {
            CallThis<std::uint32_t>(addr::cSC4Vehicle_EnterState, self, entered);
            At<std::uint32_t>(self, base::kMission) = 3;
            Virtual<std::uint32_t>(self, slot::kSlot102, 7u, Virtual<std::uint32_t>(self, slot::kMission));
            Virtual<std::uint32_t>(self, slot::kSetSiren, 0u);
            StopSound(self);
            return 1;
        }
        if (type == 0x34)
            Virtual<std::uint32_t>(self, slot::kSetSiren, 1u);
        else if (type == 0x67)
            Virtual<std::uint32_t>(self, slot::kFade, 0u, Global<std::uint32_t>(addr::SC4AutomataTuning_Fade21C84));
    }
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_EnterState, self, entered);
}

// 0x00530130, on the base's +0x204 deque (12-byte elements, ten to a block):
// its size, as the game counts it (nodes x 10 + the last block's + the first
// block's - 10, the divisions truncating).
std::int32_t __fastcall Deque204Size(char* d, void*)
{
    static bool logged;
    FirstCall("Deque204Size", logged);
    const std::int32_t nodes = static_cast<std::int32_t>(At<char*>(d, 0x1C) - At<char*>(d, 0xC)) >> 2;
    const std::int32_t back = static_cast<std::int32_t>(At<char*>(d, 0x10) - At<char*>(d, 0x14)) / 12;
    const std::int32_t front = static_cast<std::int32_t>(At<char*>(d, 8) - At<char*>(d, 0)) / 12;
    return back + nodes * 10 + front - 10;
}

// 0x005304B0, the base's slot 73 (other), ret 4: without the automata flag
// 0x80 the core's (0x005059C0, its EAX). With it: AL 0 for itself, null or
// another without 0x80; AL 1 for one without the flag 0x20; else AL 1 only
// with no route (+0x23C = +0x22C) and fewer than 3 in the +0x204 deque.
std::uint32_t __fastcall BaseCanMeet(char* self, void*, char* other)
{
    static bool logged;
    FirstCall("BaseCanMeet", logged);
    if (!CheckFlag(self, 0x80u))
        return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_CanMeet, self, other);
    if (other == self || !other || !CheckFlag(other, 0x80u))
        return 0;
    if (!CheckFlag(other, 0x20u))
        return 1;
    if (At<char*>(self, kRouteFinish) != At<char*>(self, kRouteStart))
        return 0;
    const std::int32_t count = CallThis<std::int32_t>(addr::cSC4EmergencyVehicle_Deque204Size, self + kDeque204);
    return static_cast<std::uint32_t>(count) < 3u ? 1 : 0;
}

// 0x0052F9A0, the base's slot 146: AL 1 when the point (slot 143) is within
// 16 x the byte 0x00B21CB8 of the position (FSQRT at 53 bits; unordered is
// not within) and none of the occupants at its cell (the city's slot 95, then
// 0x00B43D54's slot 14; the list by +0x54) answers its slot 9 for 0x304.
std::uint32_t __fastcall BaseAtPoint(char* self, void*)
{
    static bool logged;
    FirstCall("BaseAtPoint", logged);
    const float* const point = Virtual<const float*>(self, slot::kPoint);
    const double dx = static_cast<double>(point[0]) - static_cast<double>(At<float>(self, kPosition));
    const double dy = static_cast<double>(point[1]) - static_cast<double>(At<float>(self, kPosition + 4));
    const double dz = static_cast<double>(point[2]) - static_cast<double>(At<float>(self, kPosition + 8));
    const double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
    std::int32_t bound = static_cast<std::int32_t>(Global<std::uint8_t>(addr::cSC4AutomatonBase_NearCells)) << 4;
    if (!(static_cast<double>(bound) >= distance))
        return 0;
    // The cell into an unset local and the int that held the bound.
    std::int32_t cellX = 0;
    Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), slot::kCellOf, At<float>(self, kPosition),
                           At<float>(self, kPosition + 8), &cellX, &bound);
    char* node = Virtual<char*>(Global<void*>(addr::SC4_Service43D54), slot::kOccupantsAt, cellX, bound);
    for (; node; node = At<char*>(node, kNext))
        if (Virtual<std::uint8_t>(At<void*>(node, 0), slot::kIsType, 0x304u))
            return 0;
    return 1;
}

// 0x0052FF60, the base's slot 25 (mission), ret 4: nothing when it is the
// mission or a state 0x10 is stacked (slot 57). Else stored, slot 102 (7,
// slot 26), then by the mission: 0, 2, 4, 5 the sound stopped; 1 the sound
// stopped and the message 0x0052F910 posted; 3 the state 0x64 set (slot 51),
// the flags 0x80, 0x10000, 0x800, 0x1000 on, the siren on, and with the
// group 0x4228 or 0x4202 (the occupant part's slot 22) the sound
// 0x29DE9021 or 0x09DC5728 played (0xEA451903); 6 the state 0 set, the
// flags 0x80, 0x800, 0x1000 off and 0x10000 on, the siren off, the sound
// stopped. The sound service is read once, after slot 102.
void __fastcall BaseSetMission(char* self, void*, std::uint32_t mission)
{
    static bool logged;
    FirstCall("BaseSetMission", logged);
    if (At<std::uint32_t>(self, base::kMission) == mission)
        return;
    if (Virtual<void*>(self, slot::kInState, 0x10u))
        return;
    At<std::uint32_t>(self, base::kMission) = mission;
    Virtual<std::uint32_t>(self, slot::kSlot102, 7u, Virtual<std::uint32_t>(self, slot::kMission));
    void* const sound = Global<void*>(addr::SC4_Sound43CB8);
    auto stop = [&] {
        if (sound)
            Virtual<std::uint32_t>(sound, slot::kSoundStop, kStopSound, static_cast<void*>(self), 0u, 0u, 0u);
    };
    switch (mission) {
    case 0:
    case 2:
    case 4:
    case 5:
        stop();
        return;
    case 1:
        stop();
        CallThis<void>(addr::cSC4EmergencyVehicle_PostMission, self);
        return;
    case 3: {
        Virtual<std::uint32_t>(self, slot::kSetState, 0x64u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
        SetFlag(self, 0x80u, 1u);
        SetFlag(self, 0x10000u, 1u);
        SetFlag(self, 0x800u, 1u);
        SetFlag(self, 0x1000u, 1u);
        Virtual<std::uint32_t>(self, slot::kSetSiren, 1u);
        std::uint32_t key;
        if (Virtual<std::uint8_t>(self + kVtable4, slot::kInGroup, 0x4228u))
            key = 0x29DE9021u;
        else if (Virtual<std::uint8_t>(self + kVtable4, slot::kInGroup, 0x4202u))
            key = 0x09DC5728u;
        else
            return;
        if (sound)
            Virtual<std::uint32_t>(sound, slot::kSoundStop, kPlaySound, key, static_cast<void*>(self), 0u, 0u);
        return;
    }
    case 6:
        Virtual<std::uint32_t>(self, slot::kSetState, 0u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
        SetFlag(self, 0x80u, 0u);
        SetFlag(self, 0x10000u, 1u);
        SetFlag(self, 0x800u, 0u);
        SetFlag(self, 0x1000u, 0u);
        Virtual<std::uint32_t>(self, slot::kSetSiren, 0u);
        stop();
        return;
    default:
        return;
    }
}

// 0x00530AF0, the base's slot 194 (on), ret 4: nothing when whether +0x2D0
// is set already is the low byte. Else the old siren sound +0x2D4 stopped
// (its slot 18 with 0), taken off the city's slot 47 service (its slot 7)
// and its slot 4, cleared and released. On: a new sound from the service's
// slot 5 (the occupant part, slot 5; an SC4String "automata_pullover"; 0)
// held into +0x2D4, +0x2D0 released (through its virtual
// base) and slot 99 (2, &+0x2D0) fills it. Off: +0x2D0 taken off the core's
// models (0x0050B400) and released. Then in the state 0x10 the U-Drive
// service's slot 5 (on) or 6 (off) with 0xB.
void __fastcall BaseSetSiren(char* self, void*, std::uint32_t on)
{
    static bool logged;
    FirstCall("BaseSetSiren", logged);
    const std::uint8_t want = static_cast<std::uint8_t>(on);
    if (static_cast<std::uint8_t>(At<void*>(self, base::kHeld) != nullptr) == want)
        return;
    void* const effects = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityEffects);
    if (At<void*>(self, base::kSiren)) {
        Virtual<std::uint32_t>(At<void*>(self, base::kSiren), slot::kSirenStop, 0u);
        Virtual<std::uint32_t>(effects, slot::kEffectsRemove, At<void*>(self, base::kSiren));
        Virtual<std::uint32_t>(At<void*>(self, base::kSiren), slot::kSirenSlot4);
        ClearAndRelease(self, base::kSiren);
    }
    if (want) {
        GameString name;
        CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&name),
                        reinterpret_cast<const char*>(addr::cSC4EmergencyVehicle_SirenName));
        void* const part = Virtual<void*>(self, slot::kAsOccupant);
        Assign(self, base::kSiren, Virtual<void*>(effects, slot::kEffectsMake, part, static_cast<void*>(&name), 0u));
        FreeString(name);
        if (char* const held = At<char*>(self, base::kHeld)) {
            At<char*>(self, base::kHeld) = nullptr;
            ReleaseThroughBase(held);
        }
        Virtual<std::uint32_t>(self, slot::kSlot99, 2u, static_cast<void*>(self + base::kHeld));
    } else {
        CallThis<std::uint8_t>(addr::cSC4AutomatonCore_RemoveModel, self, At<char*>(self, base::kHeld));
        if (char* const held = At<char*>(self, base::kHeld)) {
            At<char*>(self, base::kHeld) = nullptr;
            ReleaseThroughBase(held);
        }
    }
    if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x10) {
        void* const udrive = CallCdecl<void*>(addr::SC4UDrive_GetService602640);
        Virtual<std::uint32_t>(udrive, want ? slot::kUDriveOn : slot::kUDriveOff, 0xBu);
    }
}

// 0x00532290, the base's slot 93: the static string 0x00B227FC (made on
// first use, the guard 0x00B22810) assigned cSC4Vehicle's text (unless it is
// the same string) and appended the mission's text (" [at station]" to
// " [returning home]"; none above 6) and, with the automata flag 0x200,
// " repath attempts: %d" with the byte +0x2C8. Answers the string.
void* __fastcall BaseDebugText(char* self, void*)
{
    static bool logged;
    FirstCall("BaseDebugText", logged);
    auto* const text = reinterpret_cast<GameString*>(addr::cSC4EmergencyVehicle_DebugTextString);
    if (!(Global<std::uint8_t>(addr::cSC4EmergencyVehicle_DebugTextGuard) & 1)) {
        *reinterpret_cast<std::uint32_t*>(addr::cSC4EmergencyVehicle_DebugTextGuard) |= 1;
        MakeString(*text);
        CallCdecl<int>(addr::SC4_atexit, addr::cSC4EmergencyVehicle_DebugTextAtExit);
    }
    char* const vehicle = CallThis<char*>(addr::cSC4Vehicle_DebugText, self);
    char* const inner = vehicle ? vehicle + 4 : nullptr;
    char* const own = reinterpret_cast<char*>(text) + 4;
    char allocator = 0; // the game passes a byte of its stack
    if (inner != own)
        CallThis<void>(addr::SC4String_AssignRange, own, At<char*>(inner, 0), At<char*>(inner, 4),
                       static_cast<void*>(&allocator));
    static constexpr std::uintptr_t kMissionTexts[7] = {
        addr::cSC4EmergencyVehicle_MissionText0, addr::cSC4EmergencyVehicle_MissionText1,
        addr::cSC4EmergencyVehicle_MissionText2, addr::cSC4EmergencyVehicle_MissionText3,
        addr::cSC4EmergencyVehicle_MissionText4, addr::cSC4EmergencyVehicle_MissionText5,
        addr::cSC4EmergencyVehicle_MissionText6};
    const std::uint32_t mission = At<std::uint32_t>(self, base::kMission);
    if (mission <= 6)
        CallThis<void*>(addr::cSC4Advisor_F410F00, own, reinterpret_cast<const char*>(kMissionTexts[mission]));
    if (CheckFlag(self, 0x200u)) {
        GameString line;
        MakeString(line);
        CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(&line),
                        reinterpret_cast<const char*>(addr::cSC4EmergencyVehicle_RepathFormat),
                        static_cast<std::uint32_t>(At<std::uint8_t>(self, base::kByte)));
        CallThis<void>(addr::SC4String_AppendRange, own, line.begin, line.end, static_cast<void*>(&allocator));
        FreeString(line);
    }
    return text;
}

// ====================================================================================
// The frames (em2b)

// __ftol2's low 32 bits of a float: truncation toward zero; what does not
// fit the 64-bit integer (and a NaN) is the x87's 0x8000000000000000, low
// half 0.
std::uint32_t Ftol2(float value)
{
    const double v = static_cast<double>(value);
    if (!(std::fabs(v) < 9223372036854775808.0))
        return 0;
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(v));
}

void Remove(char* self) { Virtual<std::uint32_t>(self, slot::kRemove, 0u); }

// A point of three copies of the float 0x00B22814 (the game's locals).
void DefaultPoint(float (&point)[3])
{
    for (float& f : point)
        f = Global<float>(addr::cSC4EmergencyVehicle_DefaultFloat);
}

// 0x0052F870, on the base: without an occupant the one at its cell
// entered (the position from slot 7, the cell from the city's slot 95 into
// two unset locals, the occupant from the traffic network map, the city's
// slot 40, its slot 11; then slot 132 with it and three zeros). Answers
// whether it has one (the whole of EAX).
std::uint32_t __fastcall EnsureOccupant(char* self, void*)
{
    static bool logged;
    FirstCall("EnsureOccupant", logged);
    if (!At<void*>(self, kOccupant)) {
        float position[3];
        DefaultPoint(position);
        Virtual<std::uint32_t>(self, slot::kGetPosition, static_cast<void*>(position));
        std::int32_t cellX = 0, cellZ = 0; // the game's unset locals
        void* const city = Global<void*>(addr::SC4_City);
        Virtual<std::uint32_t>(city, slot::kCellOf, position[0], position[2], &cellX, &cellZ);
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kTrafficNetwork);
        void* const occupant = Virtual<void*>(map, slot::kOccupantAt, cellX, cellZ);
        Virtual<std::uint32_t>(self, slot::kEnterOccupant, occupant, 0u, 0u, 0u);
    }
    return At<void*>(self, kOccupant) ? 1 : 0;
}

// 0x0052F910, on the base: the message 0xCA8DCDB4 (a 0x2C-byte cGZMessage
// of RZ::operator_new, no null test) with slot 192's word, the byte +0x2C8
// and the occupant part (slot 5) posted to the message server 0x00B43CCC
// (read first) and let go (a tail jump to its Release).
std::uint32_t __fastcall PostMission(char* self, void*)
{
    static bool logged;
    FirstCall("PostMission", logged);
    void* const server = Global<void*>(addr::SC4_MessageServer);
    char* const block = CallCdecl<char*>(addr::RZ_operator_new, std::size_t{0x2C});
    char* const message = block ? CallThis<char*>(addr::cGZMessage_cGZMessage, block) : nullptr;
    Virtual<std::uint32_t>(message, kAddRef);
    Virtual<std::uint32_t>(message, slot::kMessageType, kMissionMessage);
    Virtual<std::uint32_t>(message, slot::kMessageData1, Virtual<std::uint32_t>(self, slot::kSlot192));
    Virtual<std::uint32_t>(message, slot::kMessageData2, static_cast<std::uint32_t>(At<std::uint8_t>(self, base::kByte)));
    Virtual<std::uint32_t>(message, slot::kMessageObject, Virtual<void*>(self, slot::kAsOccupant));
    Virtual<std::uint32_t>(server, slot::kPost, static_cast<void*>(message), 0u);
    return Release(message);
}

// 0x0052F3F0, the base's slot 72 (two words), ret 8: cSC4Vehicle's frame
// (0x0055BA20); with the automata flag 0x80 and not slot 85, slot 147 (the
// occupant, the byte 0x00B21CC0) and the sound service told the source moved
// (0xCA45190F, this, the position as __ftol2's ints). AL the frame's.
std::uint32_t __fastcall BaseFrame(char* self, void*, std::uint32_t a, std::uint32_t b)
{
    static bool logged;
    FirstCall("BaseFrame", logged);
    const std::uint8_t framed = CallThis<std::uint8_t>(addr::cSC4Vehicle_Frame, self, a, b);
    if (CheckFlag(self, 0x80u) && !Virtual<std::uint8_t>(self, slot::kSlot85)) {
        Virtual<std::uint32_t>(self, slot::kSlot147, At<void*>(self, kOccupant),
                               static_cast<std::uint32_t>(Global<std::uint8_t>(addr::cSC4EmergencyVehicle_Slot147Byte)));
        if (void* const sound = Global<void*>(addr::SC4_Sound43CB8)) {
            const std::uint32_t z = Ftol2(At<float>(self, kPosition + 8));
            const std::uint32_t y = Ftol2(At<float>(self, kPosition + 4));
            const std::uint32_t x = Ftol2(At<float>(self, kPosition));
            Virtual<std::uint32_t>(sound, slot::kSoundStop, kMoveSound, static_cast<void*>(self), x, y, z);
        }
    }
    return framed;
}

// 0x0052FD50, the base's slot 195 (seconds), ret 4, the state 0x64 (en
// route): unless slot 87, with an occupant the move (the automaton base's
// MoveToDestination with the flag 0x2000, else cSC4Vehicle's MoveOnPath);
// at the end of the way (slot 87), not slot 85 or 165, slot 79 with slot 88
// or else Remove(false). Remove(false) without an occupant after 0x0052F870.
// With a route and the flag 0x20, +0xB0 with its flag 0x20 is given slot
// 148's object (SC4RefPtr::Assign). AL 1.
std::uint32_t __fastcall EnRouteFrame(char* self, void*, std::uint32_t seconds)
{
    static bool logged;
    FirstCall("EnRouteFrame", logged);
    if (!Virtual<std::uint8_t>(self, slot::kSlot87) && At<void*>(self, kOccupant)) {
        if (CheckFlag(self, 0x2000u))
            CallThis<std::uint32_t>(addr::cSC4AutomatonBase_MoveToDestination, self, seconds);
        else
            CallThis<std::uint32_t>(addr::cSC4Vehicle_MoveOnPath, self, seconds);
    }
    if (Virtual<std::uint8_t>(self, slot::kSlot87) && !Virtual<std::uint8_t>(self, slot::kSlot85) &&
        !Virtual<std::uint8_t>(self, slot::kSlot165)) {
        if (Virtual<std::uint8_t>(self, slot::kSlot88))
            Virtual<std::uint32_t>(self, slot::kSlot79);
        else
            Remove(self);
    }
    if (!static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_EnsureOccupant, self))) {
        Remove(self);
        return 1;
    }
    if (At<char*>(self, kRouteFinish) != At<char*>(self, kRouteStart) && CheckFlag(self, 0x20u)) {
        char* const b0 = At<char*>(self, kObjectB0);
        if (b0 && CheckFlag(b0, 0x20u))
            CallThis<void*>(addr::SC4RefPtr_Assign, self + kObjectB0, Virtual<void*>(self, slot::kSlot148));
    }
    return 1;
}

// The seconds the wait state 7 is pushed for: 0x00B21CB4 x 1000 (FLD, FMUL
// at 53 bits, FSTP float).
float RetryWait()
{
    return static_cast<float>(static_cast<double>(Global<float>(addr::cSC4EmergencyVehicle_RetryWait)) *
                              static_cast<double>(Global<float>(addr::cSC4AutomatonBase_Thousand)));
}

// 0x0052FE50, the base's slot 196 (seconds), ret 4, the state 0x65 (route
// blocked): the city's path finder (slot 43). Without an occupant after
// 0x0052F870 and with repaths counted (+0x2C8), Remove(false). Else with the
// finder its slot 17 (this, from {0, 0, unset}, to {1, the point +0x2B8}, 0,
// 3): a way found sets the mission 3, pops the state and zeroes the count.
// Otherwise the count one more: above the byte 0x00B21CB9 Remove(false);
// else 0x0052F910 and the wait state 7 pushed. AL 1.
std::uint32_t __fastcall BlockedFrame(char* self, void*, std::uint32_t)
{
    static bool logged;
    FirstCall("BlockedFrame", logged);
    void* const finder = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityPathFinder);
    if (!static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_EnsureOccupant, self)) &&
        At<std::uint8_t>(self, base::kByte)) {
        Remove(self);
        return 1;
    }
    if (finder) {
        std::uint32_t from[4] = {0, 0, 0, 0}; // the last two are the game's unset locals
        std::uint32_t to[4] = {1, At<std::uint32_t>(self, base::kPoint), At<std::uint32_t>(self, base::kPoint + 4),
                               At<std::uint32_t>(self, base::kPoint + 8)};
        if (Virtual<std::uint32_t>(finder, slot::kFindPath, static_cast<void*>(self), static_cast<void*>(from),
                                   static_cast<void*>(to), 0u, 3u)) {
            Virtual<std::uint32_t>(self, slot::kSetMission, 3u);
            Virtual<std::uint32_t>(self, slot::kPopState);
            At<std::uint8_t>(self, base::kByte) = 0;
            return 1;
        }
    }
    const std::uint8_t count = ++At<std::uint8_t>(self, base::kByte);
    if (count > Global<std::uint8_t>(addr::cSC4EmergencyVehicle_MostRepaths)) {
        Remove(self);
        return 1;
    }
    CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_PostMission, self);
    Virtual<std::uint32_t>(self, slot::kPushState, 7u, 0u, RetryWait(), 0u, 0u, 0u, 0u);
    return 1;
}

// 0x0052F570, the base's slot 198 (seconds), ret 4, the state 0x67
// (returning home): nothing while the smoothed value +0x1B0 is above 0.6 (or
// unordered) or slot 85; slot 33 (1, the float 0x00B21C84); Remove(false)
// without the target +0x2C4, or within 16 x the byte 0x00B21CB8 of it (its
// slot 8; this one's slot 7; the differences stored as floats but the z
// multiplied unrounded by its float). Else the mission 6, and with the city's
// path finder (held for the call) its slot 17 (this, from {1, the position},
// to {0, the target, unset}, 2, 2): found, the byte zero and the state 0 set
// (slot 51); not, Remove(false). AL 1.
std::uint32_t __fastcall HomeFrame(char* self, void*, std::uint32_t)
{
    static bool logged;
    FirstCall("HomeFrame", logged);
    const double value = CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, self + kSmoothed1B0);
    if (!(value <= static_cast<double>(Global<float>(addr::SC4_Float0_6))))
        return 1;
    if (Virtual<std::uint8_t>(self, slot::kSlot85))
        return 1;
    Virtual<std::uint32_t>(self, slot::kFade, 1.0f, Global<std::uint32_t>(addr::SC4AutomataTuning_Fade21C84));
    void* const target = At<void*>(self, base::kTarget);
    if (!target) {
        Remove(self);
        return 1;
    }
    float there[3];
    DefaultPoint(there);
    Virtual<std::uint32_t>(target, slot::kTargetPosition, static_cast<void*>(there));
    float here[3];
    DefaultPoint(here);
    Virtual<std::uint32_t>(self, slot::kGetPosition, static_cast<void*>(here));
    const float dx = static_cast<float>(static_cast<double>(here[0]) - static_cast<double>(there[0]));
    const float dy = static_cast<float>(static_cast<double>(here[1]) - static_cast<double>(there[1]));
    const double dzWide = static_cast<double>(here[2]) - static_cast<double>(there[2]);
    const float dz = static_cast<float>(dzWide);
    double sum = dzWide * static_cast<double>(dz);
    sum = sum + static_cast<double>(dy) * static_cast<double>(dy);
    sum = sum + static_cast<double>(dx) * static_cast<double>(dx);
    const double distance = std::sqrt(sum);
    const std::int32_t bound = static_cast<std::int32_t>(Global<std::uint8_t>(addr::cSC4AutomatonBase_NearCells)) << 4;
    if (!(static_cast<double>(bound) <= distance)) {
        Remove(self);
        return 1;
    }
    Virtual<std::uint32_t>(self, slot::kSetMission, 6u);
    void* const finder = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityPathFinder);
    if (!finder) {
        Remove(self);
        return 1;
    }
    Virtual<std::uint32_t>(finder, kAddRef);
    std::uint32_t to[4] = {0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(At<void*>(self, base::kTarget))),
                           0, 0}; // the last two are the game's unset locals
    std::uint32_t from[4];
    CallThis<void*>(addr::SC4PathEnd_FromPoint, static_cast<void*>(from), self + kPosition);
    if (Virtual<std::uint32_t>(finder, slot::kFindPath, static_cast<void*>(self), static_cast<void*>(from),
                               static_cast<void*>(to), 2u, 2u)) {
        At<std::uint8_t>(self, base::kByte) = 0;
        Virtual<std::uint32_t>(self, slot::kSetState, 0u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
    } else {
        Remove(self);
    }
    Release(finder);
    return 1;
}

// ====================================================================================
// The fire truck (em3a)

constexpr std::size_t kSmoothed1A0 = 0x1A0, kWord1F8 = 0x1F8;
constexpr std::uint32_t kFireSimulator = 0xEBFB0404, kFireInterface = 0x4C1E8345; // 0x00532AE0's
constexpr std::uint32_t kBurnSimulator = 0x621CDA33, kBurnInterface = 0x49B43F57; // 0x00533410's

// __ftol2's low 32 bits of a double (the x87's 0x8000000000000000 where it
// does not fit, low half 0).
std::uint32_t FtolWide(double value)
{
    if (!(std::fabs(value) < 9223372036854775808.0))
        return 0;
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
}

// A point of three copies of the float 0x00B22818 (the fire truck's locals).
void FirePoint(float (&point)[3])
{
    for (float& f : point)
        f = Global<float>(addr::cSC4FireTruck_DefaultFloat);
}

std::uint32_t Mission(char* self) { return Virtual<std::uint32_t>(self, slot::kMission); }

// The interface `iid` of the city's simulator `id` (simulators slot 7, not
// null-tested), into `out` (null when not given); whether its query said so.
bool SimulatorInterface(std::uint32_t id, std::uint32_t iid, void** out)
{
    void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCitySimulators);
    void* const simulator = Virtual<void*>(simulators, slot::kSimulatorById, id);
    if (!simulator)
        return false;
    return Virtual<std::uint8_t>(simulator, 0x00, iid, static_cast<void*>(out)) != 0;
}

// 0x00532AE0 (int* total, int* near), ret 8: both zero; the city's simulator
// 0xEBFB0404 as 0x4C1E8345: *total its slot 5, then the cell under the
// position (the city's slot 95 into the arguments' own stack slots) and *near
// its slot 4 over the cells within slot 193 x 3 x 0.0625 (53 bits, __ftol2) of
// it (x - r, z - r, x + r, z + r). The interface released when held.
void __fastcall FiresNear(char* self, void*, std::int32_t* total, std::int32_t* nearby)
{
    static bool logged;
    FirstCall("FiresNear", logged);
    *total = 0;
    *nearby = 0;
    void* fires = nullptr;
    if (SimulatorInterface(kFireSimulator, kFireInterface, &fires)) {
        *total = Virtual<std::int32_t>(fires, slot::kFireCount);
        // The game's outs are the arguments' stack slots: what slot 95 does
        // not write stays the pointers.
        std::uint32_t cellX = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(total));
        std::uint32_t cellZ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(nearby));
        Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), slot::kCellOf, At<float>(self, kPosition),
                               At<float>(self, kPosition + 8), &cellX, &cellZ);
        const double reach = Virtual<double>(self, slot::kReach) *
                             static_cast<double>(Global<float>(addr::cSC4AutomatonBase_FrontGap)) *
                             static_cast<double>(Global<float>(addr::SC4_FloatSixteenth));
        const std::uint32_t r = FtolWide(reach);
        std::uint32_t rect[4] = {cellX - r, cellZ - r, cellX + r, cellZ + r};
        *nearby = Virtual<std::int32_t>(fires, slot::kFiresIn, static_cast<void*>(rect));
    }
    if (fires)
        Release(fires);
}

// 0x00532BF0, the fire truck's slot 193: the city's slot 57 service's slot
// 24 for the target +0x2C4 when it knows it (slot 7; the service not
// null-tested), else 16. In ST0 (ours a double).
double __fastcall Reach(char* self, void*)
{
    static bool logged;
    FirstCall("Reach", logged);
    void* const targets = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityTargets);
    void* const target = At<void*>(self, base::kTarget);
    if (target && Virtual<std::uint8_t>(targets, slot::kTargetKnown, target))
        return Virtual<double>(targets, slot::kTargetReach, At<void*>(self, base::kTarget));
    return 16.0;
}

// The crew made: a block of 0x2C4 bytes (0x00544890, the pedestrians'
// allocator) given to cSC4SimFireCrew's constructor with this truck, into
// +0x2E0 (held, the old one released).
void MakeCrew(char* self)
{
    void* const block = CallCdecl<void*>(addr::cSC4Pedestrian_New, std::size_t{0x2C4});
    void* const crew = block ? CallThis<void*>(addr::cSC4SimFireCrew_Construct, block, self) : nullptr;
    Assign(self, fire::kCrew, crew);
}

// 0x00532C40: without a crew +0x2E0 one made; its word +0x274 the float
// 0x00B09B18's bits, or the service 0x00B43D78's word for the target when it
// knows it (slots 7 and 10; the service held for the while, its slots called
// without a null test); the city's slot 44 object told of it (slot 7) and
// the crew sent (0x00547EF0).
void __fastcall SendCrew(char* self, void*)
{
    static bool logged;
    FirstCall("SendCrew", logged);
    if (At<void*>(self, fire::kCrew))
        return;
    MakeCrew(self);
    void* const service = Global<void*>(addr::SC4_Service43D78);
    std::uint32_t word = Global<std::uint32_t>(addr::cSC4FireTruck_CrewWord);
    if (service)
        Virtual<std::uint32_t>(service, kAddRef);
    void* const target = At<void*>(self, base::kTarget);
    if (target && Virtual<std::uint8_t>(service, slot::kServiceHas, target)) {
        std::uint32_t out = 0;
        word = *Virtual<std::uint32_t*>(service, slot::kServiceWord, &out, At<void*>(self, base::kTarget));
    }
    CallThis<void>(addr::cSC4SimFireCrew_SetWord274, At<void*>(self, fire::kCrew), &word);
    void* const crews = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
    Virtual<std::uint32_t>(crews, slot::kCrewAdd, At<void*>(self, fire::kCrew));
    CallThis<void>(addr::cSC4SimFireCrew_Dispatch, At<void*>(self, fire::kCrew));
    if (service)
        Release(service);
}

// 0x00532D30: with a crew the city's slot 44 object told it is done with
// (slot 8, the crew and 0), then +0x2E0 cleared and released (a tail jump).
// EAX the last call's.
std::uint32_t __fastcall RecallCrew(char* self, void*)
{
    static bool logged;
    FirstCall("RecallCrew", logged);
    if (!At<void*>(self, fire::kCrew))
        return 0;
    void* const crews = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
    const std::uint32_t told = Virtual<std::uint32_t>(crews, slot::kCrewDone, At<void*>(self, fire::kCrew), 0u);
    void* const crew = At<void*>(self, fire::kCrew);
    if (!crew)
        return told;
    At<void*>(self, fire::kCrew) = nullptr;
    return Release(crew);
}

std::uint32_t Recall(char* self) { return CallThis<std::uint32_t>(addr::cSC4FireTruck_RecallCrew, self); }

// The mission field set without slot 25, and slot 102 (7, slot 26).
void MissionWithout25(char* self, std::uint32_t mission)
{
    At<std::uint32_t>(self, base::kMission) = mission;
    Virtual<std::uint32_t>(self, slot::kSlot102, 7u, Mission(self));
}

// 0x00532D80, the fire truck's slot 60 (event, flag), ret 8: with a crew the
// events 0 and 1 recall it (0x00532D30). The event 7 with the flag's low
// byte set: with a crew the mission 2 (the field and slot 102 (7, slot 26))
// and the crew recalled; without, when |the smoothed value +0x1A0| is below
// 0.001 (0x00A867A4; not when unordered) the mission 5 so and the crew sent
// (0x00532C40). The event 8 without a crew the same by slot 38. Then the
// base's slot 60 (0x0052F780), its EAX.
std::uint32_t __fastcall FireOnEvent(char* self, void*, std::uint32_t event, std::uint32_t flag)
{
    static bool logged;
    FirstCall("FireOnEvent", logged);
    if (At<void*>(self, fire::kCrew) && (event == 0 || event == 1))
        Recall(self);
    bool test = false;
    double value = 0;
    if (event == 7) {
        if (flag & 0xFF) {
            if (At<void*>(self, fire::kCrew)) {
                MissionWithout25(self, 2u);
                Recall(self);
            } else {
                value = std::fabs(CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, self + kSmoothed1A0));
                test = true;
            }
        }
    } else if (event == 8 && !At<void*>(self, fire::kCrew)) {
        value = Virtual<double>(self, slot::kGetSpeed);
        test = true;
    }
    if (test && value < static_cast<double>(Global<float>(addr::cSC4AutomatonBase_ArriveDistance))) {
        MissionWithout25(self, 5u);
        CallThis<void>(addr::cSC4FireTruck_SendCrew, self);
    }
    return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_OnEvent, self, event, flag);
}

// 0x00532E60, the fire truck's slot 111 (state), ret 4: leaving the state
// 0x10 recalls the crew; then the base's (0x0052F1C0), its EAX.
std::uint32_t __fastcall FireLeaveState(char* self, void*, char* left)
{
    static bool logged;
    FirstCall("FireLeaveState", logged);
    if (left && At<std::uint32_t>(left, state::kType) == 0x10)
        Recall(self);
    return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_LeaveState, self, left);
}

// 0x00533030 (from), ret 4: a 12-byte step copied (the object held, the
// word, the byte); answers this.
void* __fastcall StepCopy(char* self, void*, const char* from)
{
    static bool logged;
    FirstCall("StepCopy", logged);
    void* const object = At<void*>(const_cast<char*>(from), 0);
    At<void*>(self, 0) = object;
    if (object)
        Virtual<std::uint32_t>(object, kAddRef);
    At<std::uint32_t>(self, 4) = At<std::uint32_t>(const_cast<char*>(from), 4);
    At<std::uint8_t>(self, 8) = At<std::uint8_t>(const_cast<char*>(from), 8);
    return self;
}

// 0x00533060, the fire truck's slot 199: out of the state 0x10 the crew
// recalled and the mission 2 (slot 25).
void __fastcall FireSlot199(char* self, void*)
{
    static bool logged;
    FirstCall("FireSlot199", logged);
    if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x10)
        return;
    Recall(self);
    Virtual<void>(self, slot::kSetMission, 2u);
}

// 0x00533090, the fire truck's slot 121 (controls, word), ret 8: with the
// controls' float +0x9C at or above zero and 0x00B43DD0's slot 60 finding a
// point for the words +0x214/+0x218 (into three copies of 0x00B22818), the
// point kept on the map (x below 0 to 0, from 16 x the city's slot 93 on to
// that less 1, stored as floats; z the same by slot 94 but kept unrounded;
// neither without a city) and within the float 0x00B21CDC of the position
// (the differences as floats, summed z, y, x squared, FSQRT at 53 bits; past
// the reach, along the way at the reach: x unrounded until stored, y and z
// by floats) goes to +0x21C. Then cSC4Vehicle's 0x0055C600 (controls, word),
// its EAX.
std::uint32_t __fastcall FireSteer(char* self, void*, char* controls, std::uint32_t word)
{
    static bool logged;
    FirstCall("FireSteer", logged);
    if (At<float>(controls, controls::kHeld) >= Global<float>(addr::SC4_FloatZero)) {
        float aim[3];
        FirePoint(aim);
        void* const view = Global<void*>(addr::SC4_View43DD0);
        if (Virtual<std::uint8_t>(view, slot::kPick, At<std::uint32_t>(controls, controls::kScreen),
                                  At<std::uint32_t>(controls, controls::kScreen + 4), static_cast<void*>(aim), 1u,
                                  1u)) {
            const double zero = Global<float>(addr::SC4_FloatZero);
            const double one = Global<float>(addr::SC4_FloatOneA81228);
            double z = aim[2];
            if (void* const city = Global<void*>(addr::SC4_City)) {
                const float width = static_cast<float>(
                    static_cast<std::int32_t>(Virtual<std::uint32_t>(city, slot::kCityWidth) << 4));
                const float depth = static_cast<float>(
                    static_cast<std::int32_t>(Virtual<std::uint32_t>(city, slot::kCityDepth) << 4));
                if (aim[0] < zero)
                    aim[0] = 0.0f;
                else if (aim[0] >= width) // not when unordered
                    aim[0] = static_cast<float>(static_cast<double>(width) - one);
                if (z < zero)
                    z = zero;
                else if (z >= depth)
                    z = static_cast<double>(depth) - one;
            }
            const double reach = Global<float>(addr::cSC4FireTruck_HoseReach);
            const float dx = static_cast<float>(static_cast<double>(aim[0]) - At<float>(self, kPosition));
            float dy = static_cast<float>(static_cast<double>(aim[1]) - At<float>(self, kPosition + 4));
            float dz = static_cast<float>(z - At<float>(self, kPosition + 8));
            double sum = static_cast<double>(dz) * dz;
            sum = sum + static_cast<double>(dy) * dy;
            sum = sum + static_cast<double>(dx) * dx;
            const double distance = std::sqrt(sum);
            if (distance > reach) {
                const double inverse = Global<float>(addr::cSC4FireTruck_One) / static_cast<double>(static_cast<float>(distance));
                const double x = static_cast<double>(dx) * inverse;
                dy = static_cast<float>(static_cast<double>(dy) * inverse);
                dz = static_cast<float>(static_cast<double>(dz) * inverse);
                const double nx = x * reach + At<float>(self, kPosition);
                const float ty = static_cast<float>(static_cast<double>(dy) * reach);
                const float tz = static_cast<float>(static_cast<double>(dz) * reach);
                const double ny = static_cast<double>(ty) + At<float>(self, kPosition + 4);
                z = static_cast<float>(static_cast<double>(tz) + At<float>(self, kPosition + 8));
                aim[0] = static_cast<float>(nx);
                aim[1] = static_cast<float>(ny);
            }
            At<float>(controls, controls::kAim + 8) = static_cast<float>(z);
            At<float>(controls, controls::kAim) = aim[0];
            At<float>(controls, controls::kAim + 4) = aim[1];
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Steer, self, controls, word);
}

// 0x00533290 (on the +0x204 deque), the last 12-byte step: the finish's
// current less 0xC, from the end of the block before when at a block's first.
void* __fastcall DequeBack(char* deque, void*)
{
    static bool logged;
    FirstCall("DequeBack", logged);
    char* current = At<char*>(deque, 0x10);
    if (current == At<char*>(deque, 0x14))
        current = At<char**>(deque, 0x1C)[-1] + 0x78;
    return current - 0xC;
}

// 0x005332C0, the fire truck's slot 165: with the automata flags 0x80 and
// 0x40, not 0x2000, an occupant, no route (+0x23C = +0x22C), steps in the
// +0x204 deque and at the point (slot 146): the last step copied
// (0x00533290, 0x00533030); when its object's network part (slot 49)
// reaches (slot 21: 1, the point slot 143, a point of three 0x00B22818, 0)
// the object entered (slot 132: it, slot 136, 0, 1), +0x1F8 zero, the flag
// 0x2000 on, the point the destination (slot 10) and the flag 4 off. The
// copy's object released (no null test); AL 1. Else the automaton base's
// 0x00553AA0, its EAX.
std::uint32_t __fastcall FireSlot165(char* self, void*)
{
    static bool logged;
    FirstCall("FireSlot165", logged);
    if (!CheckFlag(self, 0x80u) || !CheckFlag(self, 0x40u) || CheckFlag(self, 0x2000u) || !At<void*>(self, kOccupant) ||
        At<char*>(self, kRouteFinish) != At<char*>(self, kRouteStart) ||
        At<char*>(self, kDeque204 + 0x10) == At<char*>(self, kDeque204) || !Virtual<std::uint8_t>(self, slot::kAtPoint))
        return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_NextStep, self);
    char step[12];
    CallThis<void*>(addr::SC4AutomatonStep_Copy, static_cast<void*>(step),
                    CallThis<void*>(addr::SC4AutomatonDeque204_Back, self + kDeque204));
    void* const object = At<void*>(step, 0);
    float point[3];
    FirePoint(point);
    if (void* const part = Virtual<void*>(object, slot::kNetworkPart)) {
        void* const at = Virtual<void*>(self, slot::kPoint);
        if (Virtual<std::uint8_t>(part, slot::kPartReaches, 1u, at, static_cast<void*>(point), 0u)) {
            const std::uint32_t groups = Virtual<std::uint32_t>(self, slot::kGroups);
            Virtual<std::uint32_t>(self, slot::kEnterOccupant, object, groups, 0u, 1u);
            At<std::uint32_t>(self, kWord1F8) = 0;
            SetFlag(self, 0x2000u, 1u);
            Virtual<std::uint32_t>(self, slot::kSetDestination, static_cast<void*>(point));
            SetFlag(self, 4u, 0u);
        }
    }
    Release(object);
    return 1;
}

// A list of the allocator 0x00B207E4 as the game keeps it on the stack: the
// sentinel and the next word (here the bits of slot 193's float).
struct NodeList {
    char* head;
    std::uint32_t word;
};

// 0x00533410 (int* burning, int* near), ret 8: *burning the city's simulator
// 0x621CDA33 as 0x49B43F57: its slot 11's slot 16 (released), the interface
// released; *near the occupants (the city's slot 34, held for the while, its
// slot 36 with the filter +0x2D8 and -1, not null-tested) in the box of
// slot 193 (as a float) about the position (slot 7 into three 0x00B22818; x
// and z less and plus it as floats, y the floats' extremes), counted through
// the list (then its slot 48). The list made (the allocator fetched from the
// framework when unset) and destroyed (0x00413FE0).
void __fastcall DouseTargets(char* self, void*, std::int32_t* burning, std::int32_t* nearby)
{
    static bool logged;
    FirstCall("DouseTargets", logged);
    *burning = 0;
    void* found = nullptr;
    SimulatorInterface(kBurnSimulator, kBurnInterface, &found);
    if (found) {
        if (void* const object = Virtual<void*>(found, slot::kBurning)) {
            *burning = Virtual<std::int32_t>(object, slot::kBurningCount);
            Release(object);
        }
        Release(found);
    }
    *nearby = 0;
    float position[3];
    FirePoint(position);
    if (!Global<void*>(addr::SC4List_NodeAllocator2)) {
        void* allocator = nullptr;
        if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
            Virtual<std::uint8_t>(framework, 0x14, std::uint32_t{0x3AE4BEA3}, std::uint32_t{0x3AE4BEAB},
                                  static_cast<void*>(&allocator));
        *reinterpret_cast<void**>(addr::SC4List_NodeAllocator2) = allocator;
        CallThis<void>(addr::SC4_ReleaseRef, static_cast<void*>(&allocator));
    }
    char* const head = Virtual<char*>(Global<void*>(addr::SC4List_NodeAllocator2), 0x0C, std::uint32_t{0xC});
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    NodeList list{head, 0};
    const float reach = static_cast<float>(Virtual<double>(self, slot::kReach));
    std::memcpy(&list.word, &reach, 4);
    Virtual<std::uint32_t>(self, slot::kGetPosition, static_cast<void*>(position));
    float low[3], high[3];
    low[0] = static_cast<float>(static_cast<double>(position[0]) - reach);
    high[1] = 3.40282347e38f;
    low[1] = -3.40282347e38f;
    low[2] = static_cast<float>(static_cast<double>(position[2]) - reach);
    high[0] = static_cast<float>(static_cast<double>(position[0]) + reach);
    high[2] = static_cast<float>(static_cast<double>(position[2]) + reach);
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityOccupants);
    if (occupants)
        Virtual<std::uint32_t>(occupants, kAddRef);
    if (Virtual<std::uint8_t>(occupants, slot::kOccupantsInBox, static_cast<void*>(&list), static_cast<void*>(low),
                              static_cast<void*>(high), At<void*>(self, fire::kFilter), 0xFFFFFFFFu)) {
        std::int32_t count = 0;
        for (char* node = At<char*>(list.head, 0); node != list.head; node = At<char*>(node, 0))
            ++count;
        *nearby = count;
        Virtual<std::uint32_t>(occupants, slot::kListDone, static_cast<void*>(&list));
    }
    Release(occupants);
    CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
}

// 0x005335F0, the fire truck's slot 197 (seconds), ret 4, the state 0x66 (on
// the scene): 0x00533410 and 0x00532AE0. On the mission 2 without a crew,
// something near (either's second count above zero) the mission 5 (slot 25);
// nothing near on the mission 5 out of the state 0x10 the mission 2; on the
// mission 2 with neither first count, home (slot 29). AL 1.
std::uint32_t __fastcall FireSceneFrame(char* self, void*, std::uint32_t)
{
    static bool logged;
    FirstCall("FireSceneFrame", logged);
    std::int32_t burning = 0, near1 = 0, fires = 0, near2 = 0;
    CallThis<void>(addr::cSC4FireTruck_DouseTargets, self, &burning, &near1);
    CallThis<void>(addr::cSC4FireTruck_FiresNear, self, &fires, &near2);
    if (Mission(self) == 2 && !At<void*>(self, fire::kCrew) && (near1 > 0 || near2 > 0)) {
        Virtual<void>(self, slot::kSetMission, 5u);
        return 1;
    }
    if (near1 == 0 && near2 == 0 && Mission(self) == 5 && Virtual<std::uint32_t>(self, slot::kStateType) != 0x10) {
        Virtual<void>(self, slot::kSetMission, 2u);
        return 1;
    }
    if (Mission(self) == 2 && burning == 0 && fires == 0)
        Virtual<void>(self, slot::kGoHome);
    return 1;
}

// 0x005336C0, the fire truck's slot 25 (mission), ret 4: the base's
// (0x0052FF60); then, unless a state 0x10 is stacked (slot 57), by slot 26:
// 1 the crew recalled and a new one made, the city's slot 44 object told
// (slot 7) and sent (0x00547EF0); 5 the crew recalled, then 0x00533410 and
// 0x00532AE0, and with either second count above zero or in the state 0x10
// the crew sent (0x00532C40); else the crew recalled.
void __fastcall FireSetMission(char* self, void*, std::uint32_t mission)
{
    static bool logged;
    FirstCall("FireSetMission", logged);
    CallThis<void>(addr::cSC4EmergencyVehicle_SetMission, self, mission);
    if (Virtual<void*>(self, slot::kInState, 0x10u))
        return;
    const std::uint32_t now = Mission(self);
    Recall(self);
    if (now == 1) {
        MakeCrew(self);
        void* const crews = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
        Virtual<std::uint32_t>(crews, slot::kCrewAdd, At<void*>(self, fire::kCrew));
        CallThis<void>(addr::cSC4SimFireCrew_Dispatch, At<void*>(self, fire::kCrew));
        return;
    }
    if (now != 5)
        return;
    std::int32_t burning = 0, near1 = 0, fires = 0, near2 = 0;
    CallThis<void>(addr::cSC4FireTruck_DouseTargets, self, &burning, &near1);
    CallThis<void>(addr::cSC4FireTruck_FiresNear, self, &fires, &near2);
    if (near1 > 0 || near2 > 0 || Virtual<std::uint32_t>(self, slot::kStateType) == 0x10)
        CallThis<void>(addr::cSC4FireTruck_SendCrew, self);
}

// ====================================================================================
// The patrol car (em3b)

constexpr std::size_t kModelsBegin = 0x110, kModelsEnd = 0x114, kStartKey = 0x11C; // the core's
constexpr float kRiotRadius = 20.0f;              // 0x41A00000, pushed by slots 25 and 200
constexpr std::uint32_t kRiotWait = 0x44FA0000;    // 2000.0, slot 25's state's third word
constexpr std::uint32_t kCalmWait = 0x459C4000;    // 5000.0, slot 200's for the rioter
constexpr std::uint32_t kCalmFade = 0x3E4CCCCD;    // 0.2, slot 200's fade

// The city's simulators' slot 10 (no null tests).
bool SimulatorsBusy()
{
    void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCitySimulators);
    return Virtual<std::uint8_t>(simulators, slot::kSimulatorsBusy) != 0;
}

// 0x00543D60's body (slot 199 inlines it): with the riot police +0x2D8 the
// city's slot 44 object told it is done with (slot 8, it and 0), then +0x2D8
// cleared and released. EAX the last call's.
std::uint32_t RecallPoliceBody(char* self)
{
    if (!At<void*>(self, patrol::kCrew))
        return 0;
    void* const crews = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
    const std::uint32_t told = Virtual<std::uint32_t>(crews, slot::kCrewDone, At<void*>(self, patrol::kCrew), 0u);
    void* const police = At<void*>(self, patrol::kCrew);
    if (!police)
        return told;
    At<void*>(self, patrol::kCrew) = nullptr;
    return Release(police);
}

std::uint32_t RecallPolice(char* self) { return CallThis<std::uint32_t>(addr::cSC4PatrolCar_RecallPolice, self); }
void SendPolice(char* self) { CallThis<void>(addr::cSC4PatrolCar_SendPolice, self); }

// 0x00543C70: without riot police +0x2D8 a block of 0x27C bytes (0x00544890)
// given to cSC4RiotPolice's constructor 0x00547800 with this car, held in
// +0x2D8 (the old one released); its word +0x274 (0x00546CB0) the float
// 0x00B09FFC's bits, or the police simulator's word for the target when it
// knows it (slots 22 and 25; held for the while, its slots called without a
// null test); the city's slot 44 object told of it (slot 7) and the police
// sent (0x00546F30).
void __fastcall PatrolSendPolice(char* self, void*)
{
    static bool logged;
    FirstCall("PatrolSendPolice", logged);
    if (At<void*>(self, patrol::kCrew))
        return;
    void* const block = CallCdecl<void*>(addr::cSC4Pedestrian_New, std::size_t{0x27C});
    Assign(self, patrol::kCrew, block ? CallThis<void*>(addr::cSC4RiotPolice_Construct, block, self) : nullptr);
    void* const police = Global<void*>(addr::SC4_PoliceSimulator);
    std::uint32_t word = Global<std::uint32_t>(addr::cSC4PatrolCar_PoliceWord);
    if (police)
        Virtual<std::uint32_t>(police, kAddRef);
    void* const target = At<void*>(self, base::kTarget);
    if (target && Virtual<std::uint8_t>(police, slot::kPoliceHas, target)) {
        std::uint32_t out = 0;
        word = *Virtual<std::uint32_t*>(police, slot::kPoliceWord, &out, At<void*>(self, base::kTarget));
    }
    CallThis<void>(addr::cSC4SimFireCrew_SetWord274, At<void*>(self, patrol::kCrew), &word);
    void* const crews = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityCrewService);
    Virtual<std::uint32_t>(crews, slot::kCrewAdd, At<void*>(self, patrol::kCrew));
    CallThis<void>(addr::cSC4RiotPolice_Dispatch, At<void*>(self, patrol::kCrew));
    if (police)
        Release(police);
}

// 0x00543D60: the riot police recalled (RecallPoliceBody).
std::uint32_t __fastcall PatrolRecallPolice(char* self, void*)
{
    static bool logged;
    FirstCall("PatrolRecallPolice", logged);
    return RecallPoliceBody(self);
}

// 0x00543B40, the patrol car's slot 109 (seconds), ret 4: AL 0 without a
// current state (slot 56); in the state 0x68 slot 200 (seconds) and AL 1;
// else the base's (0x0052F490), its EAX.
std::uint32_t __fastcall PatrolStateFrame(char* self, void*, std::uint32_t seconds)
{
    static bool logged;
    FirstCall("PatrolStateFrame", logged);
    char* const current = Virtual<char*>(self, slot::kCurrentState);
    if (!current)
        return 0;
    if (At<std::uint32_t>(current, state::kType) != riot::kState)
        return CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_StateFrame, self, seconds);
    Virtual<std::uint32_t>(self, slot::kRiotFrame, seconds);
    return 1;
}

// 0x00543B80, the patrol car's slot 111 (state), ret 4: the base's
// (0x0052F1C0); when it answers AL 0 and the state left is 0x68, the start
// model again (slot 19 with 0) and AL 1; with no state the base's EAX.
std::uint32_t __fastcall PatrolLeaveState(char* self, void*, char* left)
{
    static bool logged;
    FirstCall("PatrolLeaveState", logged);
    const std::uint32_t answer = CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_LeaveState, self, left);
    if (answer & 0xFF)
        return 1;
    if (!left)
        return answer;
    if (At<std::uint32_t>(left, state::kType) == riot::kState)
        Virtual<std::uint32_t>(self, slot::kSwapStartModel, 0u);
    return 1;
}

// 0x00543BC0, the patrol car's slot 193: the city's slot 65 service's slot
// 23 for the target +0x2C4 when its slot 22 knows it (the service fetched
// first, not null-tested), else 16. In ST0 (ours a double).
double __fastcall PatrolReach(char* self, void*)
{
    static bool logged;
    FirstCall("PatrolReach", logged);
    void* const police = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityPolice);
    void* const target = At<void*>(self, base::kTarget);
    if (target && Virtual<std::uint8_t>(police, slot::kPoliceHas, target))
        return Virtual<double>(police, slot::kPoliceReach, At<void*>(self, base::kTarget));
    return 16.0;
}

// 0x00543C10, the patrol car's slot 197 (seconds), ret 4, the state 0x66 (on
// the scene): unless the city's simulators are busy (slot 10, asked first),
// on the mission 2 home (slot 29) and on the mission 5 the mission 2 (slot
// 25). AL 1.
std::uint32_t __fastcall PatrolSceneFrame(char* self, void*, std::uint32_t)
{
    static bool logged;
    FirstCall("PatrolSceneFrame", logged);
    const bool busy = SimulatorsBusy();
    const std::uint32_t mission = Mission(self);
    if (mission == 2 && !busy)
        Virtual<void>(self, slot::kGoHome);
    else if (mission == 5 && !busy)
        Virtual<void>(self, slot::kSetMission, 2u);
    return 1;
}

// 0x00543E80, the patrol car's slot 110 (state), ret 4: entering the state
// 0x10 recalls the riot police (0x00543D60); then the base's (0x0052F7C0):
// AL 1 when it answers AL 1, else whether there is a state.
std::uint32_t __fastcall PatrolEnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("PatrolEnterState", logged);
    if (entered && At<std::uint32_t>(entered, state::kType) == 0x10)
        RecallPolice(self);
    if (CallThis<std::uint32_t>(addr::cSC4EmergencyVehicle_EnterState, self, entered) & 0xFF)
        return 1;
    return entered ? 1 : 0;
}

// 0x00543F10, the patrol car's slot 199: the riot police recalled (0x00543D60
// inlined), then the mission 2 (slot 25).
void __fastcall PatrolSlot199(char* self, void*)
{
    static bool logged;
    FirstCall("PatrolSlot199", logged);
    RecallPoliceBody(self);
    Virtual<void>(self, slot::kSetMission, 2u);
}

// 0x00543F60, the patrol car's slot 25 (mission), ret 4: the base's
// (0x0052FF60); with a state 0x10 stacked (slot 57) the riot police
// recalled. Else the city's simulators asked (slot 10), then by slot 26: 1
// the police recalled and sent again (0x00543C70); 5 the police recalled,
// 0x00B43D70 asked for a riot (slot 9) and a rioter (slot 10) within 20 of
// the position, and while the simulators are busy the police sent; else the
// state 0x68 (or 7 with neither found) pushed (slot 52: 0, 2000.0, 0, 0, 0,
// 0); other missions the police recalled.
void __fastcall PatrolSetMission(char* self, void*, std::uint32_t mission)
{
    static bool logged;
    FirstCall("PatrolSetMission", logged);
    CallThis<void>(addr::cSC4EmergencyVehicle_SetMission, self, mission);
    if (Virtual<void*>(self, slot::kInState, 0x10u)) {
        RecallPolice(self);
        return;
    }
    const bool busy = SimulatorsBusy();
    const std::uint32_t now = Mission(self);
    RecallPolice(self);
    if (now == 1) {
        SendPolice(self);
        return;
    }
    if (now != 5)
        return;
    void* const riots = Global<void*>(addr::SC4_Service43D70);
    float* const position = &At<float>(self, kPosition);
    void* const riot = Virtual<void*>(riots, slot::kRiotNear, position, kRiotRadius);
    void* const rioter = Virtual<void*>(riots, slot::kRioterNear, position, kRiotRadius);
    if (busy) {
        SendPolice(self);
        return;
    }
    Virtual<void>(self, slot::kPushState, riot || rioter ? riot::kState : 7u, 0u, kRiotWait, 0u, 0u, 0u, 0u);
}

// 0x00544060, the patrol car's slot 200 (seconds), ret 4, the state 0x68 (the
// riot), on the top state record's timer +0x10 (the stack not tested). At or
// below 0 (or NaN): the state popped (slot 53) once the first model's frame
// (slot 11) reaches its count (slot 12) less 1 (no null tests). Else the
// timer less the seconds, stored as a float; when that (unrounded) is at or
// below 0: 0x00B43D70's rioters within 20 (slot 10) each told slot 11 (1),
// five at most, or without any the riot (slot 9) faded (slot 33: 0, 0.2) and
// given the state 7 (slot 52: 0, 5000.0, 1, 0, 0, 0); the siren off (slot
// 194); then with a start key (+0x11C, its third word +0x124 set) and
// models, the first swapped (0x0050E620) for the start key with the third
// word the word 0x00A99F78 at the index __ftol2(the police simulator's word
// for the target (slots 22, 25) as a float x 2.5 (0x00A95530); 0 without),
// and the first model's slot 16 (1). AL 1.
std::uint32_t __fastcall PatrolRiotFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("PatrolRiotFrame", logged);
    char* const record = At<char**>(self, kStateStack + 4)[-1];
    const double zero = Global<float>(addr::SC4_FloatZero);
    const float timer = At<float>(record, riot::kTimer);
    if (!(timer > zero)) {
        char* const first = *At<char**>(self, kModelsBegin);
        const std::int32_t last = Virtual<std::int32_t>(first, slot::kModelFrames) - 1;
        if (Virtual<std::int32_t>(first, slot::kModelFrame) >= last)
            Virtual<std::uint32_t>(self, slot::kPopState);
        return 1;
    }
    const double left = static_cast<double>(timer) - seconds;
    At<float>(record, riot::kTimer) = static_cast<float>(left);
    if (!(left <= zero))
        return 1;
    void* const riots = Global<void*>(addr::SC4_Service43D70);
    float* const position = &At<float>(self, kPosition);
    void* const riot = Virtual<void*>(riots, slot::kRiotNear, position, kRiotRadius);
    void* rioter = Virtual<void*>(riots, slot::kRioterNear, position, kRiotRadius);
    if (rioter) {
        for (int calmed = 0; rioter && calmed < 5; ++calmed) {
            Virtual<std::uint32_t>(rioter, slot::kRioterStop, 1u);
            rioter = Virtual<void*>(riots, slot::kRioterNear, position, kRiotRadius);
        }
    } else if (riot) {
        Virtual<std::uint32_t>(riot, slot::kFade, 0u, kCalmFade);
        Virtual<std::uint32_t>(riot, slot::kPushState, 7u, 0u, kCalmWait, 1u, 0u, 0u, 0u);
    }
    Virtual<std::uint32_t>(self, slot::kSetSiren, 0u);
    std::uint32_t key[3] = {At<std::uint32_t>(self, kStartKey), At<std::uint32_t>(self, kStartKey + 4),
                            At<std::uint32_t>(self, kStartKey + 8)};
    if (!key[2] || At<char*>(self, kModelsBegin) == At<char*>(self, kModelsEnd))
        return 1;
    void* const police = Global<void*>(addr::SC4_PoliceSimulator);
    std::uint32_t index = 0;
    if (police)
        Virtual<std::uint32_t>(police, kAddRef);
    void* const target = At<void*>(self, base::kTarget);
    if (target && Virtual<std::uint8_t>(police, slot::kPoliceHas, target)) {
        std::uint32_t out = 0;
        const float* const word =
            Virtual<const float*>(police, slot::kPoliceWord, &out, At<void*>(self, base::kTarget));
        index = FtolWide(static_cast<double>(*word) * Global<float>(addr::cSC4PatrolCar_ModelScale));
    }
    key[2] = Global<std::uint32_t>(static_cast<std::uintptr_t>(addr::cSC4PatrolCar_RiotModels + index * 4u));
    CallThis<void>(addr::cSC4AutomatonCore_SwapModel, self, *At<char**>(self, kModelsBegin),
                   static_cast<const std::uint32_t*>(key));
    if (char* const first = *At<char**>(self, kModelsBegin))
        Virtual<std::uint32_t>(first, slot::kModelPlayMode, 1u);
    if (police)
        Release(police);
    return 1;
}

// ====================================================================================
// The route (part em4)

// 0x00532660 (ret 0xC), the route extended from its last cell (x, z) to the
// nearest occupant at the goal's cell: the cell of the point (the city's
// slot 95, unrounded floats in, into unset locals), 0x00B43D54's occupants
// at it (slot 25: the vector, the rect of that one cell, the byte 0x00B21CB8,
// 0, 0x1C49; nothing more when it answers false); a path finder of
// 0x00B43DA8's (slot 6, never released) from the cell (x, z) with the
// game's settings (13: 2, 1; 18: 0; 5: 1 and 2; 9: 3) into a route of its
// own (slot 8); for each occupant nearer than the best so far (its cell by
// slot 59, read unsigned, squared distance to the goal's cell in floats by
// 0x005D6FC0; ordered, the first bound FLT_MAX) the route emptied and the
// finder run to its cell (slots 20, 23); an answer 1 keeps the route
// (SC4RouteDeque::Assign) and the distance (stored as a float). With one
// kept, it is inserted at the end of the route +0x22C (0x005320B0). Both
// routes destroyed, the vector freed. The finder and 0x00B43D54 are not
// null-tested.
void __fastcall ExtendRoute(char* self, void*, std::uint32_t x, std::uint32_t z, const float* point)
{
    static bool logged;
    FirstCall("ExtendRoute", logged);
    void* const network = Global<void*>(addr::SC4_Service43D54);
    struct {
        void** begin;
        void** end;
        void** capacity;
    } found = {};
    std::int32_t cellX = 0, cellZ = 0;
    Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), slot::kCellOf, point[0], point[2], &cellX, &cellZ);
    std::int32_t rect[4] = {cellX, cellZ, cellX, cellZ};
    if (Virtual<std::uint8_t>(network, slot::kOccupantsInRect, static_cast<void*>(&found), static_cast<void*>(rect),
                              static_cast<std::uint32_t>(Global<std::uint8_t>(addr::cSC4AutomatonBase_NearCells)), 0u,
                              0x1C49u)) {
        void* finder = nullptr;
        Virtual<void>(Global<void*>(addr::SC4_Service43DA8), slot::kFinderGet, static_cast<void*>(&finder));
        alignas(8) char route[kRouteSize] = {};
        CallThis<void>(addr::SC4RouteDeque_Init, route, 0u);
        alignas(8) char best[kRouteSize] = {};
        CallThis<void>(addr::SC4RouteDeque_Init, best, 0u);
        Virtual<void>(finder, slot::kFinderFrom, x, z, x, z);
        Virtual<void>(finder, slot::kFinderSlot13, 2u, 1u);
        Virtual<void>(finder, slot::kFinderSlot18, 0u);
        Virtual<void>(finder, slot::kFinderSlot5, 1u);
        Virtual<void>(finder, slot::kFinderSlot5, 2u);
        Virtual<void>(finder, slot::kFinderSlot9, 3u);
        Virtual<void>(finder, slot::kFinderRoute, static_cast<void*>(route));
        const float goal[2] = {static_cast<float>(cellX), static_cast<float>(cellZ)};
        const std::uint32_t count = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(reinterpret_cast<char*>(found.end) - reinterpret_cast<char*>(found.begin)) >> 2);
        float nearest = 3.402823466e+38f; // FLT_MAX, an immediate in the game
        bool kept = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            // Unset locals in the game: z's held the 0x00B43D54 pointer.
            std::int32_t occupantX = 0;
            std::int32_t occupantZ = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(network));
            Virtual<void>(found.begin[i], slot::kOccupantCell, &occupantX, &occupantZ);
            const float at[2] = {static_cast<float>(static_cast<double>(static_cast<std::uint32_t>(occupantX))),
                                 static_cast<float>(static_cast<double>(static_cast<std::uint32_t>(occupantZ)))};
            const double distance = CallCdecl<double>(addr::SC4_DistanceSquared2D, goal, at);
            if (!(distance < nearest))
                continue;
            CallThis<void>(addr::SC4CellDeque_Clear, route);
            Virtual<void>(finder, slot::kFinderTo, occupantX, occupantZ, occupantX, occupantZ);
            if (Virtual<std::int32_t>(finder, slot::kFinderRun) == 1) {
                nearest = static_cast<float>(distance);
                CallThis<void*>(addr::SC4RouteDeque_Assign, best, static_cast<const void*>(route));
                kept = true;
            }
        }
        if (kept) {
            const std::uint32_t* const finish = &At<std::uint32_t>(self, kRouteFinish);
            const std::uint32_t* const from = &At<std::uint32_t>(best, 0);
            CallThis<void>(addr::SC4RouteDeque_InsertRange, self + kRouteStart, finish[0], finish[1], finish[2],
                           finish[3], from[0], from[1], from[2], from[3], from[4], from[5], from[6], from[7]);
        }
        CallThis<void>(addr::SC4RouteDeque_Destroy, best);
        CallThis<void>(addr::SC4RouteDeque_Destroy, route);
    }
    if (found.begin)
        CallCdecl<void>(addr::SC4_Free, static_cast<void*>(found.begin));
}

// 0x005329B0, the base's slot 80 (point, target), ret 8: the core's
// (0x00505860); the point into +0x2B8; with the automata flag 0x400 it off
// (slot 30) and slot 33 (1.0, 0); the target into +0x2C4 unless it is null
// and one is there (the old released before the new is held, as the game).
// With a route (+0x23C not +0x22C) and 0x00B43D54, when an occupant at the
// route's last cell (slot 14; the list by +0x54) answers its slot 9 for
// 0x304, the route extended from that cell to the point (0x00532660).
void __fastcall BaseSetGoal(char* self, void*, const float* point, void* target)
{
    static bool logged;
    FirstCall("BaseSetGoal", logged);
    CallThis<void>(addr::cSC4AutomatonCore_Slot80, self, point, target);
    const std::uint32_t* const words = reinterpret_cast<const std::uint32_t*>(point);
    At<std::uint32_t>(self, base::kPoint) = words[0];
    At<std::uint32_t>(self, base::kPoint + 4) = words[1];
    At<std::uint32_t>(self, base::kPoint + 8) = words[2];
    if (CheckFlag(self, 0x400u)) {
        Virtual<void>(self, slot::kSetFlag, 0x400u, 0u);
        Virtual<void>(self, slot::kFade, 1.0f, 0.0f);
    }
    if (target || !At<void*>(self, base::kTarget)) {
        if (void* const old = At<void*>(self, base::kTarget))
            Release(old);
        At<void*>(self, base::kTarget) = target;
        if (target)
            Virtual<std::uint32_t>(target, kAddRef);
    }
    char* last = At<char*>(self, kRouteFinish);
    if (last == At<char*>(self, kRouteStart))
        return;
    if (last == At<char*>(self, kRouteFinish + 4))
        last = At<char**>(self, kRouteFinish + 0xC)[-1] + kRouteBlock;
    void* const network = Global<void*>(addr::SC4_Service43D54);
    if (!network)
        return;
    const std::uint32_t x = static_cast<std::uint8_t>(last[-3]);
    const std::uint32_t z = static_cast<std::uint8_t>(last[-2]);
    for (char* node = Virtual<char*>(network, slot::kOccupantsAt, x, z); node; node = At<char*>(node, kNext))
        if (Virtual<std::uint8_t>(At<void*>(node, 0), slot::kIsType, 0x304u)) {
            CallThis<void>(addr::cSC4EmergencyVehicle_ExtendRoute, self, x, z, point);
            return;
        }
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
            log::Write("automata emergency %s: no address entry for 0x%08X", what,
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
    log::Write("automata emergency %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Part em1.
const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4FireTruck_Create, FireTruckCreate),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Create, PatrolCarCreate),
    OPENSC4_DETOUR(addr::cSC4Ambulance_Create, AmbulanceCreate),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Construct, BaseConstruct),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Construct, FireTruckConstruct),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Construct, PatrolCarConstruct),
    OPENSC4_DETOUR(addr::cSC4Ambulance_Construct, AmbulanceConstruct),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Destroy, BaseDestroy),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_DeletingDestructor, BaseDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Destroy, FireTruckDestroy),
    OPENSC4_DETOUR(addr::cSC4FireTruck_DeletingDestructor, FireTruckDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_DeletingDestructor, PatrolCarDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4FireTruck_GetGZCLSID, FireTruckGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_GetGZCLSID, PatrolCarGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Ambulance_GetGZCLSID, AmbulanceGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Write, BaseWrite),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Read, BaseRead),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Write, FireTruckWrite),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Read, FireTruckRead),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Write, PatrolCarWrite),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Read, PatrolCarRead),
    OPENSC4_DETOUR(addr::cSC4Ambulance_Write, AmbulanceWrite),
    OPENSC4_DETOUR(addr::cSC4Ambulance_Read, AmbulanceRead),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Init, BaseInit),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Shutdown, BaseShutdown),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Shutdown, FireTruckShutdown),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Shutdown, PatrolCarShutdown),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Word, BaseWord),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_SetWord, BaseSetWord),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Mission, BaseMission),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Point, BasePoint),
    OPENSC4_DETOUR(addr::cSC4Ambulance_Slot192, AmbulanceSlot192),
    OPENSC4_DETOUR(addr::cSC4Ambulance_Slot193, AmbulanceSlot193),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Slot135, BaseSlot135),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_GoHome, BaseGoHome),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Slot105, BaseSlot105),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_OnEvent, BaseOnEvent),
};

// Part em2a.
const Detour kStates[] = {
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_LeaveState, BaseLeaveState),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Blocked, BaseBlocked),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_StateFrame, BaseStateFrame),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_EnterState, BaseEnterState),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Deque204Size, Deque204Size),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_CanMeet, BaseCanMeet),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_AtPoint, BaseAtPoint),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_SetMission, BaseSetMission),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_SetSiren, BaseSetSiren),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_DebugText, BaseDebugText),
};

// Part em2b.
const Detour kFrames[] = {
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_EnsureOccupant, EnsureOccupant),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_PostMission, PostMission),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_Frame, BaseFrame),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_EnRouteFrame, EnRouteFrame),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_BlockedFrame, BlockedFrame),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_HomeFrame, HomeFrame),
};

// Part em3a.
const Detour kFireTruck[] = {
    OPENSC4_DETOUR(addr::cSC4FireTruck_FiresNear, FiresNear),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Reach, Reach),
    OPENSC4_DETOUR(addr::cSC4FireTruck_SendCrew, SendCrew),
    OPENSC4_DETOUR(addr::cSC4FireTruck_RecallCrew, RecallCrew),
    OPENSC4_DETOUR(addr::cSC4FireTruck_OnEvent, FireOnEvent),
    OPENSC4_DETOUR(addr::cSC4FireTruck_LeaveState, FireLeaveState),
    OPENSC4_DETOUR(addr::SC4AutomatonStep_Copy, StepCopy),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Slot199, FireSlot199),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Steer, FireSteer),
    OPENSC4_DETOUR(addr::SC4AutomatonDeque204_Back, DequeBack),
    OPENSC4_DETOUR(addr::cSC4FireTruck_Slot165, FireSlot165),
    OPENSC4_DETOUR(addr::cSC4FireTruck_DouseTargets, DouseTargets),
    OPENSC4_DETOUR(addr::cSC4FireTruck_SceneFrame, FireSceneFrame),
    OPENSC4_DETOUR(addr::cSC4FireTruck_SetMission, FireSetMission),
};

// Part em3b.
const Detour kPatrolCar[] = {
    OPENSC4_DETOUR(addr::cSC4PatrolCar_StateFrame, PatrolStateFrame),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_LeaveState, PatrolLeaveState),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Reach, PatrolReach),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_SceneFrame, PatrolSceneFrame),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_SendPolice, PatrolSendPolice),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_RecallPolice, PatrolRecallPolice),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_EnterState, PatrolEnterState),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_Slot199, PatrolSlot199),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_SetMission, PatrolSetMission),
    OPENSC4_DETOUR(addr::cSC4PatrolCar_RiotFrame, PatrolRiotFrame),
};

// Part em4.
const Detour kRoute[] = {
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_ExtendRoute, ExtendRoute),
    OPENSC4_DETOUR(addr::cSC4EmergencyVehicle_SetGoal, BaseSetGoal),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kStates, sizeof kStates / sizeof kStates[0], "states") &&
           InstallList(kFrames, sizeof kFrames / sizeof kFrames[0], "frames") &&
           InstallList(kFireTruck, sizeof kFireTruck / sizeof kFireTruck[0], "fire truck") &&
           InstallList(kPatrolCar, sizeof kPatrolCar / sizeof kPatrolCar[0], "patrol car") &&
           InstallList(kRoute, sizeof kRoute / sizeof kRoute[0], "route");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.emergency", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4EmergencyVehicle_Construct,
                        "Our emergency vehicles (the fire truck, the patrol car, the ambulance and their base), the "
                        "objects: the three factories, the four constructors, the base's and the fire truck's "
                        "destructor bodies, the three deleting destructors and the three GetGZCLSIDs; the four Writes "
                        "and Reads; the base's Init and the base's, the fire truck's and the patrol car's Shutdowns; "
                        "the base's slots 23, 24, 26, 29, 60, 105, 135 and 143 and the ambulance's 192 and 193 (37 "
                        "detours, part em1); the base's states and mission: slots 25, 73 with its deque count, 79, "
                        "93, 109 to 111, 146 and 194 (10 detours, part em2a); the frames: slots 72, 195, 196 and 198, "
                        "the occupant at the cell and the mission message (6 detours, part em2b); the fire truck's "
                        "slots 25, 60, 111, 121, 165, 193, 197 and 199, its crew and nearby counts and the last step "
                        "(14 detours, part em3a); the patrol car's slots 25, 109, 110, 111, 193, 197, 199 and 200 and its "
                        "riot police sent and recalled (10 detours, part em3b); the base's slot 80 and the route "
                        "extended to the goal's occupant (2 detours, part em4; src/core/automata_emergency.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_emergency
