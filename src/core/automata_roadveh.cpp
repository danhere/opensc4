// Our other road vehicles, part ro1 (build 641): the trailer, the bus, the
// getaway van, the carjacked vehicle, the tank and the mayor's limo, all
// derived from cSC4Vehicle (src/core/automata_vehicle.cpp). ro1: the
// objects (the six factories and constructors, the tank's and the trailer's
// destructor bodies, the four deleting destructors, the trailer's
// QueryInterface and the six GetGZCLSIDs), the save (the five Writes and
// five Reads), the life cycle (the limo's, the tank's and the trailer's
// Init, the tank's and the trailer's Shutdown) and the queries (the
// trailer's rear point and its two counts, the tank's debug text). The
// layout is in src/core/automata_roadveh.h and the survey in
// docs/automata.md, "Part ro". Calls between functions of the part go to the
// game's addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.roadveh: the 44 detours in kObject (ro1) below.
//
// What it does. The trailer is 0x2E4 bytes from SC4::operator_new; the other
// five come from the vehicle pool's allocator (which hands sizes other than
// 0x2B8 on to SC4::operator_new) and go back through cSC4Vehicle::Delete.
// Each constructor runs cSC4Vehicle's with four zero words (the trailer
// passes its own four on) and sets its six vtables; the trailer zeroes its
// two vectors and its occupant and sets its point to (-1, -1, -1), the limo
// sets 50, 48 and 10, the tank its two highlights with their effect parts,
// its model indices to -1 and its attachments.
//
// Oddities kept: the trailer's +0x2E0 is not set by its constructor; the
// trailer's QueryInterface hands other interfaces to cSC4Vehicle's; the
// limo's Init reads its property whether or not it was initialised already
// and drops cSC4Vehicle's answer; the tank's Read answers by the stream's
// error alone (not by cSC4Vehicle's Read), takes versions 0 to 2 and reads
// the two indices only from 2 on; the tank's Write writes 0xFF for an index
// whose model is not in the core's model vector; the trailer's Read reads the
// two counts into one local and the words into the local the car loop
// zeroes before each reference, so a failed word read pushes the word before
// it again (a zero word is never pushed); its Shutdown stops and releases the
// cars but leaves the words, and its destructor frees both vectors without
// releasing the cars; the tank's debug text does not test the shot effect for
// null. The version words of the bus's, the carjacked vehicle's, the limo's
// and the tank's Reads are read into the stream argument's own stack slot,
// so a failed read compares the low word of the stream's address; ours does
// the same. Stack garbage the game reads (ours zeros): the trailer's and the
// van's version words, the trailer's counts and word when their reads fail
// before any was read. AL only in the game: the van's, the tank's Write and
// the trailer's Writes and Reads (the bus's, the carjacked vehicle's and the
// limo's pass cSC4Vehicle's EAX on; the tank's Read answers 0 or 1), the
// Inits of the limo and the tank, the trailer's Shutdown; ours zero-extends.
//
// Floats: the limo's first float is truncated as __ftol2 does (the low 32
// bits of the 64-bit result); the tank's debug text passes doubles of its
// floats, the velocity times 0.01 (0x00A9B7C8) as the x87 multiplies.
//
// Still the game's, at their addresses: cSC4Vehicle's code (part vb), the
// jumps the classes' vtables hold into it (0x005240C0, 0x0053CD00,
// 0x0053CD10, 0x0053CD20, the tank's 0x0054A740 and 0x0054A770, the
// trailer's 0x00555A50 shared with the trains), the adjustor thunks of the
// six parts, the fold 0x006F7DC0, the core's rear point 0x00505A40, the
// stream helpers, SC4PtrVector::InsertN, the effect parts, the attachments
// container and the string code.

#include "core/automata_roadveh.h"

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

namespace opensc4::automata_roadveh {
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
    log::Write("automata roadveh: first %s", name);
}

constexpr std::size_t kAddRef = 0x04, kRelease = 0x08;
constexpr std::size_t kVtable4 = 0x04, kSerializable = 0x08;
constexpr std::size_t kVehicleSize = 0x2B8;
// The core's fields this part reads: the byte +0x3C (set by a Read), the
// heading +0x58, the model vector +0x110..+0x114.
constexpr std::size_t kByte3C = 0x3C, kHeading = 0x58, kModelsBegin = 0x110, kModelsEnd = 0x114;
constexpr std::uint32_t kOccupantIid = 0xA821EF94; // the trailer's occupant is read back as this

void Release(void* object) { Virtual<std::uint32_t>(object, kRelease); }

// An object held through a virtual base (+4 its vbtable, whose second word is
// the offset from +4): released there.
void ReleaseVirtual(char* object)
{
    const std::int32_t offset = At<std::int32_t>(At<char*>(object, 4), 4);
    Release(object + 4 + offset);
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

// __ftol2: truncation toward zero, the low 32 bits of the 64-bit result (the
// x87's 0x8000000000000000 where it does not fit).
std::int32_t Ftol(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return 0;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value));
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

// A string of the game (cRZBaseString over an STL buffer, 0x14 bytes).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t word10;
};

void FreeString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        CallCdecl<void>(addr::SC4_Free, text.begin);
}

// A function-local static string of the game's: made on first use (the
// guard's bit 1; an empty string with an 8-byte buffer), its destructor
// given to SC4::atexit.
GameString* StaticString(std::uintptr_t at, std::uintptr_t guard, std::uintptr_t atExit)
{
    if (!(Global<std::uint8_t>(guard) & 1)) {
        *reinterpret_cast<std::uint32_t*>(guard) |= 1;
        GameString& text = *reinterpret_cast<GameString*>(at);
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
        CallCdecl<int>(addr::SC4_atexit, atExit);
    }
    return reinterpret_cast<GameString*>(at);
}

// ====================================================================================
// The objects

constexpr std::uintptr_t kTrailerTables[6] = {addr::cSC4TrailerVehicle_Vtable,   addr::cSC4TrailerVehicle_Vtable4,
                                              addr::cSC4TrailerVehicle_Vtable8,  addr::cSC4TrailerVehicle_VtableC,
                                              addr::cSC4TrailerVehicle_Vtable20, addr::cSC4TrailerVehicle_Vtable38};
constexpr std::uintptr_t kBusTables[6] = {addr::cSC4Bus_Vtable,   addr::cSC4Bus_Vtable4,  addr::cSC4Bus_Vtable8,
                                          addr::cSC4Bus_VtableC,  addr::cSC4Bus_Vtable20, addr::cSC4Bus_Vtable38};
constexpr std::uintptr_t kVanTables[6] = {addr::cSC4GetawayVan_Vtable,   addr::cSC4GetawayVan_Vtable4,
                                          addr::cSC4GetawayVan_Vtable8,  addr::cSC4GetawayVan_VtableC,
                                          addr::cSC4GetawayVan_Vtable20, addr::cSC4GetawayVan_Vtable38};
constexpr std::uintptr_t kCarjackedTables[6] = {
    addr::cSC4CarjackedVehicle_Vtable,   addr::cSC4CarjackedVehicle_Vtable4,  addr::cSC4CarjackedVehicle_Vtable8,
    addr::cSC4CarjackedVehicle_VtableC,  addr::cSC4CarjackedVehicle_Vtable20, addr::cSC4CarjackedVehicle_Vtable38};
constexpr std::uintptr_t kTankTables[6] = {addr::cSC4Tank_Vtable,   addr::cSC4Tank_Vtable4,  addr::cSC4Tank_Vtable8,
                                           addr::cSC4Tank_VtableC,  addr::cSC4Tank_Vtable20, addr::cSC4Tank_Vtable38};
constexpr std::uintptr_t kLimoTables[6] = {addr::cSC4MayorLimo_Vtable,   addr::cSC4MayorLimo_Vtable4,
                                           addr::cSC4MayorLimo_Vtable8,  addr::cSC4MayorLimo_VtableC,
                                           addr::cSC4MayorLimo_Vtable20, addr::cSC4MayorLimo_Vtable38};

// cSC4Vehicle's constructor with four zero words (the game builds them on the
// stack).
char* VehiclePart(char* self)
{
    return CallThis<char*>(addr::cSC4Vehicle_Construct, self, 0u, 0u, 0u, 0u);
}

// 0x00556DB0, the trailer's constructor (four words for cSC4Vehicle's),
// ret 0x10: the two vectors and the occupant zero, the point -1, -1, -1.
char* __fastcall TrailerConstruct(char* self, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                  std::uint32_t d)
{
    static bool logged;
    FirstCall("TrailerConstruct", logged);
    CallThis<char*>(addr::cSC4Vehicle_Construct, self, a, b, c, d);
    SetVtables(self, kTrailerTables);
    for (std::size_t at = trailer::kWords; at < trailer::kPoint; at += 4)
        At<std::uint32_t>(self, at) = 0;
    for (std::size_t i = 0; i < 12; i += 4)
        At<std::uint32_t>(self, trailer::kPoint + i) = 0xBF800000u;
    At<std::uint32_t>(self, trailer::kOccupant) = 0;
    return self;
}

// 0x00524360, the bus's constructor.
char* __fastcall BusConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("BusConstruct", logged);
    VehiclePart(self);
    SetVtables(self, kBusTables);
    return self;
}

// 0x00533BA0, the getaway van's constructor.
char* __fastcall VanConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("VanConstruct", logged);
    VehiclePart(self);
    SetVtables(self, kVanTables);
    return self;
}

// 0x005244C0, the carjacked vehicle's constructor.
char* __fastcall CarjackedConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("CarjackedConstruct", logged);
    VehiclePart(self);
    SetVtables(self, kCarjackedTables);
    return self;
}

// 0x0054CD90, the tank's constructor: the highlights null with their effect
// parts (SC4EffectPartB::Construct), the indices -1, the models and the shot
// null, the attachments (cSC4NetworkBridgeOccupant::AttachmentsConstruct).
char* __fastcall TankConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("TankConstruct", logged);
    VehiclePart(self);
    SetVtables(self, kTankTables);
    At<std::uint32_t>(self, tank::kHighlight1) = 0;
    CallThis<void*>(addr::SC4EffectPartB_Construct, self + tank::kPart1);
    At<std::uint32_t>(self, tank::kHighlight2) = 0;
    CallThis<void*>(addr::SC4EffectPartB_Construct, self + tank::kPart2);
    At<std::uint8_t>(self, tank::kIndex1) = 0xFF;
    At<std::uint8_t>(self, tank::kIndex2) = 0xFF;
    At<std::uint32_t>(self, tank::kTurretModel) = 0;
    At<std::uint32_t>(self, tank::kBarrelModel) = 0;
    At<std::uint32_t>(self, tank::kShot) = 0;
    CallThis<void*>(addr::cSC4NetworkBridgeOccupant_AttachmentsConstruct, self + tank::kAttachments);
    return self;
}

// 0x0053CEC0, the limo's constructor: +0x2B8 50, +0x2BC 48.0, +0x2C0 10.0.
char* __fastcall LimoConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("LimoConstruct", logged);
    VehiclePart(self);
    SetVtables(self, kLimoTables);
    At<std::uint32_t>(self, limo::kCount) = 0x32;
    At<std::uint32_t>(self, limo::kFloat2BC) = 0x42400000u;
    At<std::uint32_t>(self, limo::kFloat2C0) = 0x41200000u;
    return self;
}

// 0x00514970, the trailer's GZCOM factory (cdecl): 0x2E4 bytes from
// SC4::operator_new and the constructor with four zero words; null when the
// block is.
void* __cdecl TrailerCreate()
{
    static bool logged;
    FirstCall("TrailerCreate", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, kTrailerSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4TrailerVehicle_Construct, block, 0u, 0u, 0u, 0u);
}

// The other five factories (cdecl): a block of the size from the vehicle
// pool's allocator and the constructor (a tail jump); null when the block is.
void* PoolCreate(std::size_t size, std::uintptr_t construct)
{
    void* const block = CallCdecl<void*>(addr::cSC4Vehicle_PoolAllocate, size);
    if (!block)
        return nullptr;
    return CallThis<char*>(construct, block);
}

// 0x005146C0, the bus's factory.
void* __cdecl BusCreate()
{
    static bool logged;
    FirstCall("BusCreate", logged);
    return PoolCreate(kVehicleSize, addr::cSC4Bus_Construct);
}

// 0x005146A0, the getaway van's factory.
void* __cdecl VanCreate()
{
    static bool logged;
    FirstCall("VanCreate", logged);
    return PoolCreate(kVehicleSize, addr::cSC4GetawayVan_Construct);
}

// 0x005146E0, the carjacked vehicle's factory.
void* __cdecl CarjackedCreate()
{
    static bool logged;
    FirstCall("CarjackedCreate", logged);
    return PoolCreate(kVehicleSize, addr::cSC4CarjackedVehicle_Construct);
}

// 0x00514700, the tank's factory (0x37C bytes).
void* __cdecl TankCreate()
{
    static bool logged;
    FirstCall("TankCreate", logged);
    return PoolCreate(kTankSize, addr::cSC4Tank_Construct);
}

// 0x00514720, the limo's factory (0x2C4 bytes).
void* __cdecl LimoCreate()
{
    static bool logged;
    FirstCall("LimoCreate", logged);
    return PoolCreate(kLimoSize, addr::cSC4MayorLimo_Construct);
}

// 0x00556F40, the trailer's destructor body: the occupant released (not
// cleared), the cars' and the words' buffers freed (SC4::Free; the cars not
// released), then cSC4Vehicle's (a tail jump).
void* __fastcall TrailerDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("TrailerDestroy", logged);
    if (void* const occupant = At<void*>(self, trailer::kOccupant))
        Release(occupant);
    if (void* const cars = At<void*>(self, trailer::kCars))
        CallCdecl<void>(addr::SC4_Free, cars);
    if (void* const words = At<void*>(self, trailer::kWords))
        CallCdecl<void>(addr::SC4_Free, words);
    return CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
}

// 0x0054CEC0, the tank's destructor body: the attachments
// (cSC4NetworkBridgeOccupant::AttachmentsDestroy), the shot released, the
// two models and the two highlights released through their virtual base,
// the effect parts destructed (none of them cleared), then cSC4Vehicle's (a
// tail jump).
void* __fastcall TankDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("TankDestroy", logged);
    CallThis<void>(addr::cSC4NetworkBridgeOccupant_AttachmentsDestroy, self + tank::kAttachments);
    if (void* const shot = At<void*>(self, tank::kShot))
        Release(shot);
    if (char* const model = At<char*>(self, tank::kBarrelModel))
        ReleaseVirtual(model);
    if (char* const model = At<char*>(self, tank::kTurretModel))
        ReleaseVirtual(model);
    CallThis<void>(addr::SC4EffectPartB_Destruct, self + tank::kPart2);
    if (char* const highlight = At<char*>(self, tank::kHighlight2))
        ReleaseVirtual(highlight);
    CallThis<void>(addr::SC4EffectPartB_Destruct, self + tank::kPart1);
    if (char* const highlight = At<char*>(self, tank::kHighlight1))
        ReleaseVirtual(highlight);
    return CallThis<void*>(addr::cSC4Vehicle_Destroy, self);
}

// The deleting destructors of the pool's classes (flags), ret 4: the body,
// then with flag 1 cSC4Vehicle::Delete (cdecl: block, size).
char* DeletePooled(char* self, std::uint32_t flags, std::uintptr_t destroy, std::size_t size)
{
    CallThis<void*>(destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::cSC4Vehicle_Delete, static_cast<void*>(self), static_cast<std::uint32_t>(size));
    return self;
}

// 0x00524410, the deleting destructor of the bus, the getaway van and the
// carjacked vehicle (0x2B8 bytes).
char* __fastcall BusDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("BusDeletingDestructor", logged);
    return DeletePooled(self, flags, addr::cSC4Vehicle_Destroy, kVehicleSize);
}

// 0x0053CF60, the limo's (0x2C4 bytes).
char* __fastcall LimoDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("LimoDeletingDestructor", logged);
    return DeletePooled(self, flags, addr::cSC4Vehicle_Destroy, kLimoSize);
}

// 0x0054CE90, the tank's (its own body, 0x37C bytes).
char* __fastcall TankDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("TankDeletingDestructor", logged);
    return DeletePooled(self, flags, addr::cSC4Tank_Destroy, kTankSize);
}

// 0x00556F20, the trailer's (flags), ret 4: SC4::operator_delete, no null
// test.
char* __fastcall TrailerDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("TrailerDeletingDestructor", logged);
    CallThis<void*>(addr::cSC4TrailerVehicle_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x00555A60, the trailer's slot 0 (iid, out), ret 8: its class ID answers
// the object (AddRef, AL 1); the rest cSC4Vehicle's (a tail jump).
std::uint32_t __fastcall TrailerQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("TrailerQueryInterface", logged);
    if (iid != kTrailerClassId)
        return CallThis<std::uint32_t>(addr::cSC4Vehicle_QueryInterface, self, iid, out);
    *out = self;
    Virtual<std::uint32_t>(self, kAddRef);
    return 1;
}

// The serializable part's slot 5 of each class: its class ID.

// 0x00556E50, the trailer's.
std::uint32_t __fastcall TrailerGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("TrailerGetGZCLSID", logged);
    return kTrailerClassId;
}

// 0x005243B0, the bus's.
std::uint32_t __fastcall BusGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("BusGetGZCLSID", logged);
    return kBusClassId;
}

// 0x00533BF0, the getaway van's.
std::uint32_t __fastcall VanGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("VanGetGZCLSID", logged);
    return kVanClassId;
}

// 0x00524510, the carjacked vehicle's.
std::uint32_t __fastcall CarjackedGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("CarjackedGetGZCLSID", logged);
    return kCarjackedClassId;
}

// 0x0054CE30, the tank's.
std::uint32_t __fastcall TankGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("TankGetGZCLSID", logged);
    return kTankClassId;
}

// 0x0053CF30, the limo's.
std::uint32_t __fastcall LimoGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("LimoGetGZCLSID", logged);
    return kLimoClassId;
}

// ====================================================================================
// The save (this = the object + 8, the serializable part)

// 0x00524160, the bus's +8 slot 3 (stream), ret 4 (shared with
// cSC4TrainCar): version 2 (uint16), then cSC4Vehicle's Write; its EAX.
std::uint32_t __fastcall BusWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("BusWrite", logged);
    StreamCall(s, stream::kUint16, 2u);
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Write, part, s);
}

// 0x00557010, the bus's +8 slot 4 (stream), ret 4 (shared with
// cSC4TrainCar): AL 0 unless the version is 2; else cSC4Vehicle's Read, its
// EAX.
std::uint32_t __fastcall BusRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("BusRead", logged);
    if (VersionInArgument(s) != 2)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Read, part, s);
}

// 0x0053CE00, the carjacked vehicle's and the limo's +8 slot 3: as the
// bus's with version 1.
std::uint32_t __fastcall CarjackedWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("CarjackedWrite", logged);
    StreamCall(s, stream::kUint16, 1u);
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Write, part, s);
}

// 0x00524490, the carjacked vehicle's and the limo's +8 slot 4: as the bus's
// with version 1.
std::uint32_t __fastcall CarjackedRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("CarjackedRead", logged);
    if (VersionInArgument(s) != 1)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Read, part, s);
}

// 0x00533B00, the getaway van's +8 slot 3: version 3, cSC4Vehicle's Write;
// when true a false (SC4Stream::WriteBool) and a zero (stream slot 0x20).
// AL cSC4Vehicle's.
std::uint32_t __fastcall VanWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("VanWrite", logged);
    StreamCall(s, stream::kUint16, 3u);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Write, part, s);
    if (ok) {
        CallCdecl<void*>(addr::SC4Stream_WriteBool, s, 0u);
        StreamCall(s, stream::kSint32, 0u);
    }
    return ok;
}

// 0x00533B40, the getaway van's +8 slot 4: AL 0 unless the version is 3;
// cSC4Vehicle's Read; when true a bool (SC4Stream::ReadBool) and a word
// (stream slot 0x20) read and dropped. AL cSC4Vehicle's.
std::uint32_t __fastcall VanRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("VanRead", logged);
    std::uint16_t version = 0;
    StreamCall(s, stream::kUint16, &version);
    if (version != 3)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Read, part, s);
    if (ok) {
        std::uint32_t flag = 0;
        std::int32_t word = 0;
        CallCdecl<void*>(addr::SC4Stream_ReadBool, s, static_cast<void*>(&flag));
        StreamCall(s, stream::kSint32, &word);
    }
    return ok;
}

// 0x0054AA30, the tank's +8 slot 3: version 2, cSC4Vehicle's Write; when
// true the indices of the turret and the barrel models in the core's model
// vector (+0x110; the last match, 0xFF for none) as two sint8s. AL
// cSC4Vehicle's.
std::uint32_t __fastcall TankWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("TankWrite", logged);
    char* const self = part - kSerializable;
    StreamCall(s, stream::kUint16, 2u);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Write, part, s);
    if (!ok)
        return ok;
    std::uint8_t turret = 0xFF;
    std::uint8_t barrel = 0xFF;
    void** model = At<void**>(self, kModelsBegin);
    for (std::uint32_t i = 0;
         i < static_cast<std::uint32_t>((At<char*>(self, kModelsEnd) - At<char*>(self, kModelsBegin)) >> 2); ++i) {
        if (At<void*>(self, tank::kTurretModel) == model[0])
            turret = static_cast<std::uint8_t>(i);
        if (At<void*>(self, tank::kBarrelModel) == model[0])
            barrel = static_cast<std::uint8_t>(i);
        ++model;
    }
    StreamCall(s, stream::kSint8, static_cast<std::uint32_t>(turret));
    StreamCall(s, stream::kSint8, static_cast<std::uint32_t>(barrel));
    return ok;
}

// 0x0054A8E0, the tank's +8 slot 4: AL 0 for a version above 2; else
// cSC4Vehicle's Read and, when true with a version of 2, the two indices
// (sint8s into +0x358 and +0x359); EAX 1 when the stream has no error
// (slot 0x48's AL 0), else 0.
std::uint32_t __fastcall TankRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("TankRead", logged);
    char* const self = part - kSerializable;
    const std::uint16_t version = VersionInArgument(s);
    if (version > 2)
        return 0;
    if (CallThis<std::uint8_t>(addr::cSC4Vehicle_Read, part, s) && version >= 2) {
        StreamCall(s, stream::kSint8, &At<std::uint8_t>(self, tank::kIndex1));
        StreamCall(s, stream::kSint8, &At<std::uint8_t>(self, tank::kIndex2));
    }
    return static_cast<std::uint8_t>(Virtual<std::uint32_t>(s, stream::kError)) == 0 ? 1u : 0u;
}

// 0x005562A0, the trailer's +8 slot 3: version 2, cSC4Vehicle's Write; when
// true the cars' count and a reference each (SC4Stream::WriteReference), the
// words' count and the words, the point (SC4Stream::WriteVector3), the
// occupant (a reference) and +0x2E0. AL cSC4Vehicle's.
std::uint32_t __fastcall TrailerWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("TrailerWrite", logged);
    char* const self = part - kSerializable;
    StreamCall(s, stream::kUint16, 2u);
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Write, part, s);
    if (!ok)
        return ok;
    char* const cars = self + trailer::kCars;
    StreamCall(s, stream::kUint32,
               static_cast<std::uint32_t>((At<char*>(cars, 4) - At<char*>(cars, 0)) >> 2));
    for (void** car = At<void**>(cars, 0); car != At<void**>(cars, 4); ++car)
        CallCdecl<void>(addr::SC4Stream_WriteReference, s, *car);
    char* const words = self + trailer::kWords;
    const std::uint32_t count = static_cast<std::uint32_t>((At<char*>(words, 4) - At<char*>(words, 0)) >> 2);
    StreamCall(s, stream::kUint32, count);
    for (std::uint32_t i = 0; i < count; ++i)
        StreamCall(s, stream::kUint32, At<std::uint32_t*>(words, 0)[i]);
    CallCdecl<void*>(addr::SC4Stream_WriteVector3, s, self + trailer::kPoint);
    CallCdecl<void>(addr::SC4Stream_WriteReference, s, At<void*>(self, trailer::kOccupant));
    StreamCall(s, stream::kUint32, At<std::uint32_t>(self, trailer::kWord));
    return ok;
}

// 0x00556C30, the trailer's +8 slot 4: AL 0 unless the version is 2;
// cSC4Vehicle's Read; when true the cars (a count, then per car a reference
// read as cSC4Vehicle into a local zeroed first, pushed when not null; the
// vectors are not emptied first), the words (a count into the same local,
// then each word into the cars' local, pushed when not zero), the point, the
// occupant (released and cleared, then read as 0xA821EF94) and +0x2E0. AL
// cSC4Vehicle's.
std::uint32_t __fastcall TrailerRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("TrailerRead", logged);
    char* const self = part - kSerializable;
    std::uint16_t version = 0;
    StreamCall(s, stream::kUint16, &version);
    if (version != 2)
        return 0;
    const std::uint8_t ok = CallThis<std::uint8_t>(addr::cSC4Vehicle_Read, part, s);
    if (!ok)
        return ok;
    std::uint32_t count = 0;
    std::uint32_t value = 0;
    StreamCall(s, stream::kUint32, &count);
    for (std::uint32_t i = 0; i < count; ++i) {
        value = 0;
        CallCdecl<void*>(addr::SC4Stream_ReadReference, s, trailer::kCarIid, static_cast<void*>(&value), 0u);
        if (value)
            PushWord(self + trailer::kCars, &value);
    }
    StreamCall(s, stream::kUint32, &count);
    for (std::uint32_t i = 0; i < count; ++i) {
        StreamCall(s, stream::kUint32, &value);
        if (value)
            PushWord(self + trailer::kWords, &value);
    }
    CallCdecl<void*>(addr::SC4Stream_ReadVector3, s, self + trailer::kPoint);
    if (void* const occupant = At<void*>(self, trailer::kOccupant)) {
        At<void*>(self, trailer::kOccupant) = nullptr;
        Release(occupant);
    }
    CallCdecl<void*>(addr::SC4Stream_ReadReference, s, kOccupantIid, self + trailer::kOccupant, 0u);
    StreamCall(s, stream::kUint32, &At<std::uint32_t>(self, trailer::kWord));
    return ok;
}

// ====================================================================================
// The life cycle

// 0x00555A30, the trailer's slot 3 (Init): the automata flag 0x10000000 set
// (slot 30), then cSC4Vehicle's Init (a tail jump: its EAX).
std::uint32_t __fastcall TrailerInit(char* self, void*)
{
    static bool logged;
    FirstCall("TrailerInit", logged);
    Virtual<void>(self, slot::kSetFlag, 0x10000000u, 1u);
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Init, self);
}

// 0x005563C0, the trailer's slot 4 (Shutdown): cSC4Vehicle's Shutdown; the
// occupant cleared and released; each car (the end re-read each step) given
// slot 13 (0) and released; the cars' vector emptied (the words kept). AL
// cSC4Vehicle's.
std::uint32_t __fastcall TrailerShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("TrailerShutdown", logged);
    const std::uint8_t done = CallThis<std::uint8_t>(addr::cSC4Vehicle_Shutdown, self);
    if (void* const occupant = At<void*>(self, trailer::kOccupant)) {
        At<void*>(self, trailer::kOccupant) = nullptr;
        Release(occupant);
    }
    char* const cars = self + trailer::kCars;
    for (void** car = At<void**>(cars, 0); car != At<void**>(cars, 4); ++car)
        if (void* const object = *car) {
            Virtual<void>(object, slot::kCarStop, 0u);
            Release(object);
        }
    At<void**>(cars, 4) = At<void**>(cars, 0);
    return done;
}

// 0x0053CE20, the limo's slot 3 (Init): cSC4Vehicle's Init (its answer
// dropped); the property 0xEC15654A as floats (SC4::GetPropertyFloatVector
// on the occupant part's holder, slot 6): the first truncated into +0x2B8,
// the second and third into +0x2BC and +0x2C0 when there are that many; the
// vector's buffer freed. AL 1.
std::uint32_t __fastcall LimoInit(char* self, void*)
{
    static bool logged;
    FirstCall("LimoInit", logged);
    CallThis<std::uint32_t>(addr::cSC4Vehicle_Init, self);
    void* const holder = Virtual<void*>(self + kVtable4, slot::kHolder);
    float* floats[3] = {nullptr, nullptr, nullptr};
    CallCdecl<std::uint8_t>(addr::SC4_GetPropertyFloatVector, holder, limo::kProperty, static_cast<void*>(floats));
    // SAR, then unsigned compares.
    const std::uint32_t count = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(reinterpret_cast<char*>(floats[1]) - reinterpret_cast<char*>(floats[0])) >> 2);
    if (count > 0)
        At<std::int32_t>(self, limo::kCount) = Ftol(static_cast<double>(floats[0][0]));
    if (count > 1)
        At<std::uint32_t>(self, limo::kFloat2BC) = reinterpret_cast<std::uint32_t*>(floats[0])[1];
    if (count > 2)
        At<std::uint32_t>(self, limo::kFloat2C0) = reinterpret_cast<std::uint32_t*>(floats[0])[2];
    if (floats[0])
        CallCdecl<void>(addr::SC4_Free, static_cast<void*>(floats[0]));
    return 1;
}

// A model pointer held through a virtual base, released and cleared (the
// field first).
void ClearVirtual(char* self, std::size_t at)
{
    if (char* const model = At<char*>(self, at)) {
        At<char*>(self, at) = nullptr;
        ReleaseVirtual(model);
    }
}

// 0x0054C880, the tank's slot 3 (Init): AL 1 at once when initialised (the
// occupant part's slot 5); else cSC4Vehicle's Init (its answer dropped).
// With the core's byte +0x3C set and both indices other than 0xFF: when they
// differ, are both above 0 and below the model count, the turret and barrel
// models are those models (0x004201D0, held through their virtual base);
// else nothing. Otherwise each model released and cleared and made again by
// index (core slot 99 with 1 and 2). Then the barrel angle 0.2, the turret
// angle 0, the velocity 65, not firing; the attachments bound to the barrel
// model's drawable (its virtual base, or null; slot 2), the shot effect
// "tankshot" / "fx_00_tankshot" from them (slot 9) into +0x364 (AddRef the
// new, Release the old), the attachments' slot 5, then the tank's slots 192
// and 195 (the highlights). AL 1.
std::uint32_t __fastcall TankInit(char* self, void*)
{
    static bool logged;
    FirstCall("TankInit", logged);
    if (Virtual<std::uint8_t>(self + kVtable4, slot::kInitialized))
        return 1;
    CallThis<std::uint32_t>(addr::cSC4Vehicle_Init, self);
    const std::int8_t turret = At<std::int8_t>(self, tank::kIndex1);
    const std::int8_t barrel = At<std::int8_t>(self, tank::kIndex2);
    if (At<std::uint8_t>(self, kByte3C) && turret != -1 && barrel != -1) {
        void** const models = At<void**>(self, kModelsBegin);
        const std::uint32_t count =
            static_cast<std::uint32_t>((At<char*>(self, kModelsEnd) - At<char*>(self, kModelsBegin)) >> 2);
        if (turret != barrel && turret > 0 && barrel > 0 && static_cast<std::uint32_t>(turret) < count &&
            static_cast<std::uint32_t>(barrel) < count) {
            CallThis<void*>(addr::SC4UI_F4201D0, self + tank::kTurretModel, models[turret]);
            CallThis<void*>(addr::SC4UI_F4201D0, self + tank::kBarrelModel,
                            At<void**>(self, kModelsBegin)[barrel]);
        }
    } else {
        ClearVirtual(self, tank::kTurretModel);
        Virtual<std::uint8_t>(self, slot::kModelByIndex, 1u, self + tank::kTurretModel);
        ClearVirtual(self, tank::kBarrelModel);
        Virtual<std::uint8_t>(self, slot::kModelByIndex, 2u, self + tank::kBarrelModel);
    }
    At<std::uint32_t>(self, tank::kBarrel) = 0x3E4CCCCDu;
    At<std::uint32_t>(self, tank::kTurret) = 0;
    At<std::uint32_t>(self, tank::kVelocity) = 0x42820000u;
    At<std::uint8_t>(self, tank::kFiring) = 0;
    char* drawable = At<char*>(self, tank::kBarrelModel);
    if (drawable)
        drawable = drawable + 4 + At<std::int32_t>(At<char*>(drawable, 4), 4);
    char* const attachments = self + tank::kAttachments;
    Virtual<std::uint32_t>(attachments, slot::kAttach, drawable);
    GameString name;
    CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&name),
                    reinterpret_cast<const char*>(addr::cSC4Tank_ShotName));
    GameString effect;
    CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&effect),
                    reinterpret_cast<const char*>(addr::cSC4Tank_ShotEffectName));
    void* const shot = Virtual<void*>(attachments, slot::kAttachEffect, static_cast<void*>(&effect),
                                      static_cast<void*>(&name));
    void* const old = At<void*>(self, tank::kShot);
    if (shot != old) {
        if (shot)
            Virtual<std::uint32_t>(shot, kAddRef);
        At<void*>(self, tank::kShot) = shot;
        if (old)
            Release(old);
    }
    FreeString(effect);
    FreeString(name);
    Virtual<std::uint32_t>(attachments, slot::kAttachDone);
    Virtual<void>(self, slot::kMakeHighlight1);
    Virtual<void>(self, slot::kMakeHighlight2);
    return 1;
}

// 0x0054A850, the tank's slot 4 (Shutdown): when initialised (the occupant
// part's slot 5) the two models released and cleared, the shot stopped (its
// slot 4 with 1) and, when still there, cleared and released; then
// cSC4Vehicle's Shutdown (a tail jump: its EAX).
std::uint32_t __fastcall TankShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("TankShutdown", logged);
    if (Virtual<std::uint8_t>(self + kVtable4, slot::kInitialized)) {
        ClearVirtual(self, tank::kTurretModel);
        ClearVirtual(self, tank::kBarrelModel);
        if (void* const shot = At<void*>(self, tank::kShot)) {
            Virtual<void>(shot, slot::kShotStop, 1u);
            if (void* const still = At<void*>(self, tank::kShot)) {
                At<void*>(self, tank::kShot) = nullptr;
                Release(still);
            }
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Shutdown, self);
}

// ====================================================================================
// The queries

// 0x00555AE0, the trailer's slot 66 (out), ret 4: with no cars the core's
// rear point (0x00505A40); else the last car's slot 66 (both tail jumps).
void* __fastcall TrailerRearPoint(char* self, void*, float* out)
{
    static bool logged;
    FirstCall("TrailerRearPoint", logged);
    char* const cars = self + trailer::kCars;
    if (At<void**>(cars, 0) == At<void**>(cars, 4))
        return CallThis<void*>(addr::cSC4AutomatonCore_RearPoint, self, out);
    void* const last = At<void**>(cars, 4)[-1];
    return Virtual<void*>(last, slot::kRearPoint, out);
}

// 0x00556ED0, the trailer's slot 193: the number of cars.
std::int32_t __fastcall TrailerCarCount(char* self, void*)
{
    static bool logged;
    FirstCall("TrailerCarCount", logged);
    return static_cast<std::int32_t>(At<char*>(self, trailer::kCars + 4) - At<char*>(self, trailer::kCars)) >> 2;
}

// 0x00556EE0, the trailer's slot 194: the number of words.
std::int32_t __fastcall TrailerWordCount(char* self, void*)
{
    static bool logged;
    FirstCall("TrailerWordCount", logged);
    return static_cast<std::int32_t>(At<char*>(self, trailer::kWords + 4) - At<char*>(self, trailer::kWords)) >> 2;
}

// 0x0054CAA0, the tank's slot 93: the static string 0x00B22908 formatted
// "Firing: %d, Mouse(...), Orientation<...>, TurretAngle %3.2f, BarrelAngle
// %3.2f, BarrelVelocity %3.2f" with the shot's slot 5 (no null test), the
// mouse point, the heading (+0x58), the turret and barrel angles and the
// velocity times 0.01. Answers the string.
void* __fastcall TankDebugText(char* self, void*)
{
    static bool logged;
    FirstCall("TankDebugText", logged);
    GameString* const text =
        StaticString(addr::cSC4Tank_DebugTextString, addr::cSC4Tank_DebugTextGuard, addr::cSC4Tank_DebugTextAtExit);
    const double velocity = static_cast<double>(At<float>(self, tank::kVelocity)) *
                            static_cast<double>(Global<float>(addr::cSC4Tank_VelocityScale));
    const std::uint32_t firing = Virtual<std::uint8_t>(At<void*>(self, tank::kShot), slot::kShotRunning);
    CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(text),
                    reinterpret_cast<const char*>(addr::cSC4Tank_DebugFormat), firing,
                    static_cast<double>(At<float>(self, tank::kMouse)),
                    static_cast<double>(At<float>(self, tank::kMouse + 4)),
                    static_cast<double>(At<float>(self, kHeading)), static_cast<double>(At<float>(self, kHeading + 4)),
                    static_cast<double>(At<float>(self, kHeading + 8)),
                    static_cast<double>(At<float>(self, tank::kTurret)),
                    static_cast<double>(At<float>(self, tank::kBarrel)), velocity);
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
            log::Write("automata roadveh %s: no address entry for 0x%08X", what,
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
    log::Write("automata roadveh %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Part ro1.
const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Create, TrailerCreate),
    OPENSC4_DETOUR(addr::cSC4Bus_Create, BusCreate),
    OPENSC4_DETOUR(addr::cSC4GetawayVan_Create, VanCreate),
    OPENSC4_DETOUR(addr::cSC4CarjackedVehicle_Create, CarjackedCreate),
    OPENSC4_DETOUR(addr::cSC4Tank_Create, TankCreate),
    OPENSC4_DETOUR(addr::cSC4MayorLimo_Create, LimoCreate),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Construct, TrailerConstruct),
    OPENSC4_DETOUR(addr::cSC4Bus_Construct, BusConstruct),
    OPENSC4_DETOUR(addr::cSC4GetawayVan_Construct, VanConstruct),
    OPENSC4_DETOUR(addr::cSC4CarjackedVehicle_Construct, CarjackedConstruct),
    OPENSC4_DETOUR(addr::cSC4Tank_Construct, TankConstruct),
    OPENSC4_DETOUR(addr::cSC4MayorLimo_Construct, LimoConstruct),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Destroy, TrailerDestroy),
    OPENSC4_DETOUR(addr::cSC4Tank_Destroy, TankDestroy),
    OPENSC4_DETOUR(addr::cSC4Bus_DeletingDestructor, BusDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4MayorLimo_DeletingDestructor, LimoDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4Tank_DeletingDestructor, TankDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_DeletingDestructor, TrailerDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_QueryInterface, TrailerQueryInterface),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_GetGZCLSID, TrailerGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Bus_GetGZCLSID, BusGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4GetawayVan_GetGZCLSID, VanGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4CarjackedVehicle_GetGZCLSID, CarjackedGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Tank_GetGZCLSID, TankGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4MayorLimo_GetGZCLSID, LimoGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Bus_Write, BusWrite),
    OPENSC4_DETOUR(addr::cSC4Bus_Read, BusRead),
    OPENSC4_DETOUR(addr::cSC4CarjackedVehicle_Write, CarjackedWrite),
    OPENSC4_DETOUR(addr::cSC4CarjackedVehicle_Read, CarjackedRead),
    OPENSC4_DETOUR(addr::cSC4GetawayVan_Write, VanWrite),
    OPENSC4_DETOUR(addr::cSC4GetawayVan_Read, VanRead),
    OPENSC4_DETOUR(addr::cSC4Tank_Write, TankWrite),
    OPENSC4_DETOUR(addr::cSC4Tank_Read, TankRead),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Write, TrailerWrite),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Read, TrailerRead),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Init, TrailerInit),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Shutdown, TrailerShutdown),
    OPENSC4_DETOUR(addr::cSC4MayorLimo_Init, LimoInit),
    OPENSC4_DETOUR(addr::cSC4Tank_Init, TankInit),
    OPENSC4_DETOUR(addr::cSC4Tank_Shutdown, TankShutdown),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_RearPoint, TrailerRearPoint),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_CarCount, TrailerCarCount),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_WordCount, TrailerWordCount),
    OPENSC4_DETOUR(addr::cSC4Tank_DebugText, TankDebugText),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.roadveh", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4TrailerVehicle_Construct,
                        "Our other road vehicles (the trailer, the bus, the getaway van, the carjacked vehicle, the "
                        "tank and the mayor's limo), the objects: the six factories and constructors, the tank's and "
                        "the trailer's destructor bodies, the four deleting destructors, the trailer's "
                        "QueryInterface and the six GetGZCLSIDs; the five Writes and Reads; the Inits of the limo, the "
                        "tank and the trailer and the Shutdowns of the tank and the trailer; the trailer's rear point "
                        "and counts and the tank's debug text (44 detours, part ro1; "
                        "src/core/automata_roadveh.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_roadveh
