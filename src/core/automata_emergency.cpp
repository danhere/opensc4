// Our emergency vehicles, part em1 (build 641): a base on cSC4Vehicle
// (src/core/automata_vehicle.cpp) and cSC4FireTruck, cSC4PatrolCar and
// cSC4Ambulance on it. em1: the objects (the three factories, the four
// constructors, the base's and the fire truck's destructor bodies, the three
// deleting destructors and the three GetGZCLSIDs), the save (the four Writes
// and Reads), the life cycle (the base's Init, the base's, the fire truck's
// and the patrol car's Shutdowns) and the queries (the base's slots 23, 24,
// 26, 29, 60, 105, 135 and 143, the ambulance's 192 and 193). The layout is
// in src/core/automata_emergency.h and the survey in docs/automata.md, "Part
// em". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.emergency: the 37 detours in kObject (em1) below.
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
// base's other slots (em2 to em4: 25, 72, 73, 79, 80, 93, 109 to 111, 146,
// 194 to 198) and the classes' (em3), the core's slot 105 0x005096B0 and
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

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode) { return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.emergency", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4EmergencyVehicle_Construct,
                        "Our emergency vehicles (the fire truck, the patrol car, the ambulance and their base), the "
                        "objects: the three factories, the four constructors, the base's and the fire truck's "
                        "destructor bodies, the three deleting destructors and the three GetGZCLSIDs; the four Writes "
                        "and Reads; the base's Init and the base's, the fire truck's and the patrol car's Shutdowns; "
                        "the base's slots 23, 24, 26, 29, 60, 105, 135 and 143 and the ambulance's 192 and 193 (37 "
                        "detours, part em1; src/core/automata_emergency.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_emergency
