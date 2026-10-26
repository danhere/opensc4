// Our emergency vehicles, parts em1 and em2a (build 641): a base on cSC4Vehicle
// (src/core/automata_vehicle.cpp) and cSC4FireTruck, cSC4PatrolCar and
// cSC4Ambulance on it. em1: the objects (the three factories, the four
// constructors, the base's and the fire truck's destructor bodies, the three
// deleting destructors and the three GetGZCLSIDs), the save (the four Writes
// and Reads), the life cycle (the base's Init, the base's, the fire truck's
// and the patrol car's Shutdowns) and the queries (the base's slots 23, 24,
// 26, 29, 60, 105, 135 and 143, the ambulance's 192 and 193). em2a: the
// base's states and mission (slots 25, 73 with its deque count, 79, 93, 109
// to 111, 146 and 194). The layout is in src/core/automata_emergency.h and the survey in docs/automata.md, "Part
// em". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.emergency: the 37 detours in kObject (em1) and the
//   10 in kStates (em2a) below.
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
// Still the game's, at their addresses: cSC4Vehicle's code (part vb), the
// base's other slots (em2b and em4: 72, 80, 195 to 198 and 0x0052F910) and
// the classes' (em3), the core's slot 105 0x005096B0 and
// cSC4Vehicle's slot 60 0x0055B6C0, cSC4AutomatonBase::RemoveNow, the jumps
// and adjustor thunks the vtables hold, the occupant filters' code, the
// stream helpers and the sound service.

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

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kStates, sizeof kStates / sizeof kStates[0], "states");
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
                        "93, 109 to 111, 146 and 194 (10 detours, part em2a; src/core/automata_emergency.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_emergency
