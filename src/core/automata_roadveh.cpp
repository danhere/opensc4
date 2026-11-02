// Our other road vehicles, parts ro1 to ro3b (build 641): the trailer, the bus, the
// getaway van, the carjacked vehicle, the tank and the mayor's limo, all
// derived from cSC4Vehicle (src/core/automata_vehicle.cpp). ro1: the
// objects (the six factories and constructors, the tank's and the trailer's
// destructor bodies, the four deleting destructors, the trailer's
// QueryInterface and the six GetGZCLSIDs), the save (the five Writes and
// five Reads), the life cycle (the limo's, the tank's and the trailer's
// Init, the tank's and the trailer's Shutdown) and the queries (the
// trailer's rear point and its two counts, the tank's debug text). ro2: the
// frames (the trailer's slots 72, 73, 95, 132 and 192 to 198: its cars made
// from its words and walked along the path or behind each other; the bus's
// 164 and 172, the carjacked vehicle's and the van's 101, the limo's 121 and
// the tank's 127). ro3a: the tank on frames (its slots 110, 115 and 121: the
// turret's record on entering the state 0x10, the heading turned by the
// U-Drive turn keys, the turret, barrel, velocity and shot by the keys and
// the pointer). ro3b: the tank's frame (its slot 72: the turret and barrel
// models placed by four quaternion turns). The layout is in src/core/automata_roadveh.h and the survey in
// docs/automata.md, "Part ro". Calls between functions of the part go to the
// game's addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.roadveh: the 44 detours in kObject (ro1), the 15 in
//   kFrame (ro2) and the 4 in kTankFrame (ro3a's 3, ro3b's 1) below.
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
// floats, the velocity times 0.01 (0x00A9B7C8) as the x87 multiplies. The
// frames (ro2) keep the x87's 53-bit doubles and store floats where the
// game stores them (each function's comment says which values stay
// unrounded); the van's cell is __ftol2's.
//
// ro2's oddities kept: slot 198 sets the crossing's y by the share of the
// squared distances (0x005D6FC0), not the distances; slot 197 keeps the
// car's new front y on the x87 only; slot 196 ends the whole frame (not the
// car) when the deque walk fails, keeps a null path it failed to get, and a
// one-point path makes its index -1 (about 2^32 steps back); slot 195 reads
// the word at the car count without a bounds test and rejects a position
// with any coordinate below 0; slot 132 adds one to the word count's low
// byte only; the limo's slot 121 uses the literals 50, 48 and 10, not the
// fields Init sets; the bus's slot 164 releases each answer of its query
// through the list's element and keeps its list in the argument's slot. AL
// only in the game (ours zero-extends): the trailer's slots 72, 73, 195 and 198,
// the bus's 164 and 172, both slots 101, the limo's 121.
//
// ro3a's oddities kept: slot 110 tests the state for null after cSC4Vehicle's
// slot 110 has read its type without a test; slot 115 asks the controls'
// mouse point for -1 a second time, so its branch that would clear the turn
// keys is never taken, and turns the heading about its cross with (0, 1, 0)
// with the zero 0x00A9B7A0 multiplied through the matrix; slot 121 tests
// neither 0x00B43DC4 nor the shot for null, wraps the velocity past 100 to
// 30 before clamping it, keeps its turret rate in a function-local static
// (0.12 degrees, made on first use) and its two sound timers in the game's
// statics (shared by every tank). Stack garbage the game passes (ours zero):
// the two bytes after the transform's flags. AL only in the game (ours
// zero-extends): slots 110 and 115.
//
// Still the game's, at their addresses: cSC4Vehicle's code (part vb), the
// jumps the classes' vtables hold into it (0x005240C0, 0x0053CD00,
// 0x0053CD10, 0x0053CD20, the tank's 0x0054A740 and 0x0054A770, the
// trailer's 0x00555A50 shared with the trains), the adjustor thunks of the
// six parts, the fold 0x006F7DC0, the core's rear point 0x00505A40, the
// stream helpers, SC4PtrVector::InsertN, the effect parts, the attachments
// container and the string code. ro2 calls also the core's slots 73
// (0x005059C0), 95 (0x0050F140), 101 (0x0050CE60) and 127 (0x00508D00), the
// base's slots 164 and 172, cSC4Vehicle's 72, 121 and 132, the core's
// 0x0050D870, SC4::FindUint32, SC4PtrVector::PushBack,
// SC4::SegmentsIntersect2D, 0x005D6FC0 and the list helpers. ro3a calls
// cSC4Vehicle's slots 110, 115 and 121, 0x0046B4A0, the core's smoothed
// value 0x004FA960, the U-Drive sounds (0x00602640), __CIacos (0x009F0090)
// and __ftol2's equal, and the slots of the barrel model, the attachments
// and the shot.

#include "core/automata_roadveh.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/x87.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
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
// The frames (ro2)

float Zero() { return Global<float>(addr::SC4_FloatZero); }
float Half() { return Global<float>(addr::SC4_FloatHalf); }
float Epsilon() { return Global<float>(addr::cSC4AutomatonBase_DirectionEpsilon); }
float TrailerOne() { return Global<float>(addr::cSC4TrailerVehicle_One); }
std::uint8_t Bool(std::uint32_t eax) { return static_cast<std::uint8_t>(eax); }

void AddRef(void* object) { Virtual<std::uint32_t>(object, kAddRef); }

// A point the trailer's code fills with the float 0x00B22954 before a slot
// writes it.
void FillPoint(float* point)
{
    const std::uint32_t fill = Global<std::uint32_t>(addr::cSC4TrailerVehicle_FillValue);
    std::memcpy(point + 0, &fill, 4);
    std::memcpy(point + 1, &fill, 4);
    std::memcpy(point + 2, &fill, 4);
}

// The rear point of `automaton` as slots 196 and 197 take it: its position
// (slot 7) less its direction (slot 15) times its radius (slot 17), the x
// and y products rounded to floats, the z product not.
void RearOf(char* automaton, float* point)
{
    float direction[3];
    FillPoint(point);
    FillPoint(direction);
    Virtual<std::uint32_t>(automaton, slot::kGetPosition, point);
    Virtual<std::uint32_t>(automaton, slot::kDirection, static_cast<float*>(direction));
    const double radius = Virtual<double>(automaton, slot::kGetRadius);
    const float x = static_cast<float>(direction[0] * radius);
    const float y = static_cast<float>(direction[1] * radius);
    const double z = direction[2] * radius;
    point[0] = static_cast<float>(static_cast<double>(point[0]) - x);
    point[1] = static_cast<float>(static_cast<double>(point[1]) - y);
    point[2] = static_cast<float>(point[2] - z);
}

// The fade both follow loops give each car while +0x80 (slot 32) is below 1
// and the automaton has the flag 1: the car's slot 33 (that, 0.01) and its
// slot 83 (seconds).
void FadeCar(char* self, char* car, float fade, float seconds)
{
    if (!(fade < Global<float>(addr::SC4_FloatOneA81228)) || !Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, 1u)))
        return;
    Virtual<std::uint32_t>(car, slot::kFade, fade, 0.01f);
    Virtual<std::uint32_t>(car, slot::kSlot83, seconds);
}

char* LastCarOrSelf(char* self)
{
    char** const begin = At<char**>(self, trailer::kCars);
    char** const end = At<char**>(self, trailer::kCars + 4);
    return begin == end ? self : end[-1];
}

// 0x00555B50, the trailer's slot 72 (word, seconds), ret 8: cSC4Vehicle's
// frame (0x0055BA20); while the point +0x2D0 is unset (x or z below 0) it
// takes the last car's (or its own) position (slot 7), occupant (slot 137,
// referenced) and groups (slot 136); a car more (slot 195) while there are
// more words than cars; then slot 197 with the flag 0x2000, else slot 196
// (seconds). AL the frame's.
std::uint32_t __fastcall TrailerFrame(char* self, void*, std::uint32_t word, float seconds)
{
    static bool logged;
    FirstCall("TrailerFrame", logged);
    const std::uint8_t moved = Bool(CallThis<std::uint32_t>(addr::cSC4Vehicle_Frame, self, word, seconds));
    float* const point = &At<float>(self, trailer::kPoint);
    if (point[0] < Zero() || point[2] < Zero()) {
        char* const last = LastCarOrSelf(self);
        Virtual<std::uint32_t>(last, slot::kGetPosition, point);
        void* const occupant = Virtual<void*>(last, slot::kOccupantOf);
        void* const before = At<void*>(self, trailer::kOccupant);
        if (occupant != before) {
            if (occupant)
                AddRef(occupant);
            At<void*>(self, trailer::kOccupant) = occupant;
            if (before)
                Release(before);
        }
        At<std::uint32_t>(self, trailer::kWord) = Virtual<std::uint32_t>(last, slot::kGroups);
    }
    const std::uint32_t cars = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(At<char*>(self, trailer::kCars + 4) - At<char*>(self, trailer::kCars)) >> 2);
    const std::uint32_t words = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(At<char*>(self, trailer::kWords + 4) - At<char*>(self, trailer::kWords)) >> 2);
    if (words > cars)
        Virtual<std::uint32_t>(self, slot::kAddCar);
    if (Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, 0x2000u)))
        Virtual<std::uint32_t>(self, slot::kFollowLeader, seconds);
    else
        Virtual<std::uint32_t>(self, slot::kFollowPath, seconds);
    return moved;
}

// 0x00556260, the trailer's slot 73 (automaton), ret 4: false for one of
// its cars (SC4::FindUint32, its argument's own slot passed as the value
// and the tag), else the core's slot 73 (0x005059C0). AL only for false.
std::uint32_t __fastcall TrailerCanMeet(char* self, void*, void* other)
{
    static bool logged;
    FirstCall("TrailerCanMeet", logged);
    std::uint32_t value = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(other));
    std::uint32_t* const end = At<std::uint32_t*>(self, trailer::kCars + 4);
    std::uint32_t* const found = CallCdecl<std::uint32_t*>(addr::SC4_FindUint32, At<std::uint32_t*>(self, trailer::kCars),
                                                           end, static_cast<const std::uint32_t*>(&value),
                                                           static_cast<const void*>(&value));
    if (found != end)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_CanMeet, self, other);
}

// 0x00555B10, the trailer's slot 95 (template), ret 4: the core's slot 95
// (0x0050F140), then each word of the template's list (+0x50, a circular
// list, the word at +8 of each node) given to slot 192.
void __fastcall TrailerSetTemplate(char* self, void*, char* from)
{
    static bool logged;
    FirstCall("TrailerSetTemplate", logged);
    CallThis<void>(addr::cSC4AutomatonCore_SetTemplate, self, from);
    if (!from)
        return;
    char* const head = At<char*>(from, 0x50);
    if (At<char*>(head, 0) == head)
        return;
    char* node = At<char*>(head, 0);
    if (node == head)
        return;
    do {
        Virtual<std::uint32_t>(self, slot::kPushWord, At<std::uint32_t>(node, 8));
        node = At<char*>(node, 0);
    } while (node != At<char*>(from, 0x50));
}

// 0x00555A90, the trailer's slot 132 (occupant, groups, path, keep), ret
// 0x10: slot 181 with the deque +0x27C and the number of words plus one (the
// increment on the low byte only, as the game's INC CL), then cSC4Vehicle's
// slot 132 (0x0055D5A0).
void __fastcall TrailerEnterOccupant(char* self, void*, void* occupant, std::uint32_t groups, void* path,
                                     std::uint32_t keep)
{
    static bool logged;
    FirstCall("TrailerEnterOccupant", logged);
    std::uint32_t count = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(At<char*>(self, trailer::kWords + 4) - At<char*>(self, trailer::kWords)) >> 2);
    count = (count & 0xFFFFFF00u) | ((count + 1) & 0xFFu);
    Virtual<std::uint32_t>(self, slot::kRemember, self + field::kDeque, count);
    CallThis<void>(addr::cSC4Vehicle_EnterOccupant, self, occupant, groups, path, keep);
}

// 0x00556380, the trailer's slot 192 (word), ret 4: the word pushed on +0x2B8
// (SC4PtrVector::InsertN when full, the argument's own slot as the value and
// the allocator; a null end is not written through).
void __fastcall TrailerPushWord(char* self, void*, std::uint32_t word)
{
    static bool logged;
    FirstCall("TrailerPushWord", logged);
    char* const words = self + trailer::kWords;
    std::uint32_t* const end = At<std::uint32_t*>(words, 4);
    if (end != At<std::uint32_t*>(words, 8)) {
        if (end)
            *end = word;
        At<std::uint32_t*>(words, 4) += 1;
        return;
    }
    CallThis<void>(addr::SC4PtrVector_InsertN, words, end, &word, static_cast<void*>(&word), 1u, 1u);
}

// 0x00556A30, the trailer's slot 195: a car more. False when the last car's
// (or its own) position (slot 140) has a coordinate below 0, when it is
// nearer the point +0x2D0 than twice its radius (slot 17, as a float),
// without the vehicle manager (slot 174) or without words. Else the manager's
// slot 15 makes the car of the word at the number of cars (no bounds test):
// (word, the point, +0x84, three zeros, 0, the byte +0x90, 0). It is given
// the flag 2 off (slot 30), the last car as its leader (slot 49), the
// point with the last car's direction (slots 15 and 16) and the occupant
// +0x2DC with the groups +0x2E0 (slot 132, no path, keep); pushed on the car
// vector and referenced. True then. AL only for the early falses.
std::uint32_t __fastcall TrailerAddCar(char* self, void*)
{
    static bool logged;
    FirstCall("TrailerAddCar", logged);
    void* car = nullptr;
    char* const last = LastCarOrSelf(self);
    const float radius = static_cast<float>(Virtual<double>(last, slot::kGetRadius));
    const float* const at = Virtual<float*>(last, slot::kPosition);
    const float position[3] = {at[0], at[1], at[2]};
    if (position[0] < Zero() || position[1] < Zero() || position[2] < Zero())
        return 0;
    const float* const point = &At<float>(self, trailer::kPoint);
    const double dx = static_cast<double>(position[0]) - point[0];
    const double dy = static_cast<double>(position[1]) - point[1];
    const double dz = static_cast<double>(position[2]) - point[2];
    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (!(static_cast<double>(radius) + radius <= distance))
        return 0;
    void* const manager = Virtual<void*>(self, slot::kManager);
    if (!manager)
        return 0;
    std::uint32_t* const words = At<std::uint32_t*>(self, trailer::kWords);
    if (words == At<std::uint32_t*>(self, trailer::kWords + 4))
        return 0;
    const std::int32_t index =
        static_cast<std::int32_t>(At<char*>(self, trailer::kCars + 4) - At<char*>(self, trailer::kCars)) >> 2;
    float zeros[3] = {0.0f, 0.0f, 0.0f};
    car = Virtual<void*>(manager, slot::kMakeVehicle, words[index], self + trailer::kPoint,
                         At<std::uint32_t>(self, field::kAutomataFlags), static_cast<float*>(zeros), 0u,
                         static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kByte90)), 0u);
    if (!car)
        return 0;
    Virtual<std::uint32_t>(car, slot::kSetFlag, 2u, 0u);
    Virtual<std::uint32_t>(car, slot::kSetLeader, last);
    float heading[3];
    FillPoint(heading);
    Virtual<std::uint32_t>(last, slot::kDirection, static_cast<float*>(heading));
    Virtual<std::uint32_t>(car, slot::kPlace, self + trailer::kPoint, static_cast<float*>(heading));
    Virtual<std::uint32_t>(car, slot::kEnter, At<void*>(self, trailer::kOccupant),
                           At<std::uint32_t>(self, trailer::kWord), 0u, 1u);
    CallThis<void>(addr::SC4PtrVector_PushBack, self + trailer::kCars, &car);
    AddRef(car);
    return 1;
}

// 0x005560D0, the trailer's slot 198 (centre, a, b, out), ret 0x10: where the
// segment a-b, in x and z, crosses the segment through the centre square to
// it (half-length 100 (0x00A9C360) over a-b's length). False when a-b is
// shorter than 1e-4 (0x00A9C35C) in each coordinate or nothing crosses
// (SC4::SegmentsIntersect2D). The crossing's x and z into out, its y
// between a's and b's by the share of the squared distances (0x005D6FC0, a
// to the crossing over a to b, the latter stored as a float; 0 when that is
// at most 1e-5).
std::uint32_t __fastcall TrailerCrossPath(char*, void*, const float* centre, const float* a, const float* b,
                                          float* out)
{
    static bool logged;
    FirstCall("TrailerCrossPath", logged);
    const float ex = static_cast<float>(static_cast<double>(b[0]) - a[0]);
    const double ey = static_cast<double>(b[1]) - a[1];
    const float ez = static_cast<float>(static_cast<double>(b[2]) - a[2]);
    const double tiny = Global<float>(addr::cSC4TrailerVehicle_CrossEpsilon);
    if (std::fabs(static_cast<double>(b[0]) - a[0]) < tiny && std::fabs(static_cast<double>(b[1]) - a[1]) < tiny &&
        std::fabs(static_cast<double>(b[2]) - a[2]) < tiny)
        return 0;
    const double length =
        std::sqrt((static_cast<double>(ez) * ez + ey * ey) + static_cast<double>(ex) * ex);
    const double k = static_cast<double>(TrailerOne()) / length;
    const double scale = Global<float>(addr::cSC4TrailerVehicle_CrossScale);
    const double oz = (static_cast<double>(ez) * k) * scale;
    const double ox = (static_cast<double>(ex) * k) * scale;
    const float ozf = static_cast<float>(oz);
    const float farEnd[2] = {static_cast<float>(oz + centre[0]), static_cast<float>(static_cast<double>(centre[2]) - ox)};
    const float nearEnd[2] = {static_cast<float>(static_cast<double>(centre[0]) - ozf),
                           static_cast<float>(ox + centre[2])};
    const float from[2] = {a[0], a[2]};
    const float to[2] = {b[0], b[2]};
    float cross[2];
    if (!CallCdecl<std::uint8_t>(addr::SC4_SegmentsIntersect2D, static_cast<const float*>(farEnd),
                                 static_cast<const float*>(nearEnd), static_cast<const float*>(from),
                                 static_cast<const float*>(to), static_cast<float*>(cross)))
        return 0;
    out[0] = cross[0];
    out[2] = cross[1];
    const float whole = static_cast<float>(
        CallCdecl<double>(addr::SC4_DistanceSquared2D, static_cast<const float*>(from), static_cast<const float*>(to)));
    const double part =
        CallCdecl<double>(addr::SC4_DistanceSquared2D, static_cast<const float*>(from), static_cast<const float*>(cross));
    const double share = whole > Epsilon() ? part / whole : static_cast<double>(Zero());
    out[1] = static_cast<float>((static_cast<double>(b[1]) - a[1]) * share + a[1]);
    return 1;
}

// 0x00555C70, the trailer's slot 197 (seconds), ret 4: each car behind the
// one before it (the trailer for the first). Each car is faded as the
// trailer is; then its front T is its position less its direction times its
// radius (slots 7, 15, 17). With the record of the state 0x10 (slot 57) and
// that record's +0x2C below 0, T's x and z are the rear point of the one
// before (RearOf) less twice the car's radius along the trailer's heading,
// and its y is the terrain's (0x00B43D54, slot 18 (x, z)). Else T moves by
// slot 38 times the half-sum of the car's direction and the unit vector
// from T to the rear point (the car's direction when that is within 1e-5).
// The car is placed (slot 16) its radius from the rear point towards T
// along the unit vector between them (again the direction within 1e-5),
// with that vector as its heading; slots 178 (the rear point) and 180 (the
// heading). T's new y is never stored; it stays unrounded on the x87.
void __fastcall TrailerFollowLeader(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("TrailerFollowLeader", logged);
    const float fade = static_cast<float>(Virtual<double>(self, slot::kFadeNow));
    void* const terrain = Global<void*>(addr::SC4_Service43D54);
    char* const record = Virtual<char*>(self, slot::kRecord, 0x10u);
    char* before = self;
    for (char** it = At<char**>(self, trailer::kCars); it != At<char**>(self, trailer::kCars + 4); ++it) {
        char* const car = *it;
        FadeCar(self, car, fade, seconds);
        float rear[3];
        RearOf(before, rear);
        float position[3], direction[3];
        FillPoint(position);
        FillPoint(direction);
        Virtual<std::uint32_t>(car, slot::kGetPosition, static_cast<float*>(position));
        Virtual<std::uint32_t>(car, slot::kDirection, static_cast<float*>(direction));
        const float radius = static_cast<float>(Virtual<double>(car, slot::kGetRadius));
        float front[3];
        {
            const double x = direction[0] * static_cast<double>(radius);
            const double y = direction[1] * static_cast<double>(radius);
            const float z = static_cast<float>(direction[2] * static_cast<double>(radius));
            front[0] = static_cast<float>(position[0] - x);
            front[1] = static_cast<float>(position[1] - y);
            front[2] = static_cast<float>(static_cast<double>(position[2]) - z);
        }
        double y;
        if (record && At<float>(record, 0x2C) < Zero()) {
            const float x2 = static_cast<float>((static_cast<double>(radius) * At<float>(self, field::kHeading)) * 2.0);
            const double z2 = (static_cast<double>(radius) * At<float>(self, field::kHeading + 8)) * 2.0;
            front[0] = static_cast<float>(static_cast<double>(rear[0]) - x2);
            front[2] = static_cast<float>(static_cast<double>(rear[2]) - z2);
            y = Virtual<double>(terrain, 0x48, front[0], front[2]);
        } else {
            const double dx = static_cast<double>(rear[0]) - front[0];
            const float dyf = static_cast<float>(static_cast<double>(rear[1]) - front[1]);
            const double dz = static_cast<double>(rear[2]) - front[2];
            const float dzf = static_cast<float>(dz);
            const double length = std::sqrt((dz * dzf + static_cast<double>(dyf) * dyf) + dx * dx);
            double ux, uy;
            float uz;
            if (length > Epsilon()) {
                const float inverse = static_cast<float>(static_cast<double>(TrailerOne()) / length);
                ux = dx * inverse;
                uy = static_cast<double>(dyf) * inverse;
                uz = static_cast<float>(static_cast<double>(dzf) * inverse);
            } else {
                ux = direction[0];
                uy = direction[1];
                uz = direction[2];
            }
            const double half = Half();
            const float hx = static_cast<float>(static_cast<double>(static_cast<float>(ux + direction[0])) * half);
            const float hy = static_cast<float>((uy + direction[1]) * half);
            const float hz = static_cast<float>((static_cast<double>(uz) + direction[2]) * half);
            const double step = Virtual<double>(self, slot::kStep);
            const float gy = static_cast<float>(hy * step);
            const float gz = static_cast<float>(hz * step);
            front[0] = static_cast<float>(hx * step + front[0]);
            y = static_cast<double>(gy) + front[1];
            front[2] = static_cast<float>(static_cast<double>(gz) + front[2]);
        }
        float heading[3];
        {
            const float dxf = static_cast<float>(static_cast<double>(rear[0]) - front[0]);
            const double dy = rear[1] - y;
            const double dz = static_cast<double>(rear[2]) - front[2];
            const double length = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dxf) * dxf);
            if (length > Epsilon()) {
                const double inverse = static_cast<double>(TrailerOne()) / length;
                heading[0] = static_cast<float>(static_cast<double>(dxf) * inverse);
                heading[1] = static_cast<float>(dy * inverse);
                heading[2] = static_cast<float>(dz * inverse);
            } else {
                std::memcpy(heading, direction, sizeof heading);
            }
        }
        const double px = heading[0] * static_cast<double>(radius);
        const double py = heading[1] * static_cast<double>(radius);
        const float pz = static_cast<float>(heading[2] * static_cast<double>(radius));
        float place[3] = {static_cast<float>(rear[0] - px), static_cast<float>(rear[1] - py),
                          static_cast<float>(static_cast<double>(rear[2]) - pz)};
        Virtual<std::uint32_t>(car, slot::kPlace, static_cast<float*>(place), static_cast<float*>(heading));
        Virtual<std::uint32_t>(car, slot::kSlot178, static_cast<float*>(rear));
        Virtual<std::uint32_t>(car, slot::kSlot180, static_cast<float*>(heading));
        before = car;
    }
}

// A deque iterator (cur, first, last, node) over +0x27C's blocks of 16
// elements of 8 bytes (0x80).
struct DequeIt {
    char* cur;
    char* first;
    char* last;
    char** node;
};

// The element before `it`, as the game's reverse iterator finds it: the
// offset of the one before from the block's start, its block by division
// towards zero (offsets below 0 or of 16 and more).
char* ElementBefore(const DequeIt& it)
{
    const std::int32_t offset = (static_cast<std::int32_t>(it.cur - it.first) >> 3) - 1;
    if (offset >= 0 && offset < 16)
        return it.cur - 8;
    const std::int32_t block = offset > 0 ? offset / 16 : (offset + 1) / 16 - 1;
    return it.node[block] + (offset - block * 16) * 8;
}

// The number of 12-byte points of a path (a vector: begin, end), divided
// towards zero.
std::int32_t PathCount(char** path) { return static_cast<std::int32_t>(path[1] - path[0]) / 12; }

// 0x00556460, the trailer's slot 196 (seconds), ret 4: the cars along the
// path, back from the front point. City slot 40 first (its answer dropped).
// From the path +0x1F8 at the index +0x1FC (segment a-b, b the front point
// +0x264 first), each car is faded as the trailer is and placed where a
// segment crosses the square through the rear point of the one before
// (RearOf; slot 198 on the trailer). Without a crossing a steps back along
// the path (b the old a); at index 0 the deque +0x27C is walked back one
// element, the element's occupant (its slot 49, the network part; that
// part's slot 3 (word, 0) the path) giving the next path from its last
// point but one. The walk ends the whole frame when the deque is used up or
// an element gives no path, and with no path at the start (the first car
// faded only). A placed car gets slot 178 (the crossing), slot 132 (the
// occupant and groups of the path, the path, keep), the unit vector from a
// to b halved with the car's slot 179 vector as its heading, and its
// position the crossing less the heading times half the trailer's radius
// (+0x7C) (slot 16), then slot 180 (the heading). Quirks kept: a path of one
// point makes the index -1, and the steps back then run for about 2^32
// steps; the path the walk fails to get (null) is kept.
void __fastcall TrailerFollowPath(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("TrailerFollowPath", logged);
    Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), 0xA0);
    char** path = At<char**>(self, field::kPath);
    float a[3], b[3], cross[3];
    FillPoint(a);
    FillPoint(b);
    FillPoint(cross);
    DequeIt it;
    std::memcpy(&it, self + field::kDeque, sizeof it);
    bool more = false;
    const float fade = static_cast<float>(Virtual<double>(self, slot::kFadeNow));
    void* occupant = At<void*>(self, field::kOccupant);
    std::uint32_t groups = At<std::uint32_t>(self, field::kGroups);
    std::uint32_t index = 0;
    if (path) {
        std::memcpy(b, self + field::kFront, sizeof b);
        index = At<std::uint32_t>(self, field::kPathIndex);
        more = true;
    }
    char** car = At<char**>(self, trailer::kCars);
    if (car == At<char**>(self, trailer::kCars + 4))
        return;
    char* before = self;
    for (;;) {
        FadeCar(self, *car, fade, seconds);
        float rear[3];
        RearOf(before, rear);
        for (;;) {
            if (!more)
                return;
            if (path && index < static_cast<std::uint32_t>(PathCount(path))) {
                std::memcpy(a, path[0] + index * 12, sizeof a);
                if (Bool(Virtual<std::uint32_t>(self, slot::kCrossPath, static_cast<float*>(rear), static_cast<float*>(a),
                                                static_cast<float*>(b), static_cast<float*>(cross))))
                    break;
            }
            if (index > 0) {
                --index;
                std::memcpy(b, a, sizeof b);
                continue;
            }
            more = false;
            if (it.cur == At<char*>(self, field::kDeque)) {
                std::memcpy(&it, self + field::kDeque + 0x10, sizeof it);
            } else {
                if (it.cur == it.first) {
                    --it.node;
                    it.first = *it.node;
                    it.last = it.first + 0x80;
                    it.cur = it.last;
                }
                it.cur -= 8;
            }
            if (it.cur == At<char*>(self, field::kDeque))
                continue;
            char* const element = ElementBefore(it);
            void* const object = At<void*>(element, 0);
            if (!object)
                continue;
            void* const part = Virtual<void*>(object, slot::kSetLeader);
            if (!part)
                continue;
            path = Virtual<char**>(part, 0x0C, At<std::uint32_t>(element, 4), 0u);
            if (!path)
                continue;
            index = static_cast<std::uint32_t>(PathCount(path) - 2);
            more = true;
            occupant = object;
            groups = At<std::uint32_t>(element, 4);
            std::memcpy(b, a, sizeof b);
        }
        char* const placed = *car;
        Virtual<std::uint32_t>(placed, slot::kSlot178, static_cast<float*>(cross));
        Virtual<std::uint32_t>(placed, slot::kEnter, occupant, groups, path, 1u);
        float heading[3];
        double hy, hz;
        {
            const double dx = static_cast<double>(b[0]) - a[0];
            const double dy = static_cast<double>(b[1]) - a[1];
            const double dz = static_cast<double>(b[2]) - a[2];
            const float dzf = static_cast<float>(dz);
            const float inverse =
                static_cast<float>(static_cast<double>(TrailerOne()) / std::sqrt((dz * dzf + dy * dy) + dx * dx));
            const float ux = static_cast<float>(dx * inverse);
            const float uy = static_cast<float>(dy * inverse);
            const float uz = static_cast<float>(static_cast<double>(dzf) * inverse);
            const float* const other = Virtual<float*>(placed, slot::kSlot179);
            const double sx = static_cast<double>(ux) + other[0];
            const double sy = static_cast<double>(uy) + other[1];
            const float sz = static_cast<float>(static_cast<double>(uz) + other[2]);
            const double half = Half();
            heading[0] = static_cast<float>(sx * half);
            hy = sy * half;
            hz = static_cast<double>(sz) * half;
            heading[1] = static_cast<float>(hy);
            heading[2] = static_cast<float>(hz);
        }
        const double back = static_cast<double>(At<float>(self, field::kRadius)) * Half();
        const float bx = static_cast<float>(heading[0] * back);
        const float by = static_cast<float>(hy * back);
        const double bz = hz * back;
        float place[3] = {static_cast<float>(static_cast<double>(cross[0]) - bx),
                          static_cast<float>(static_cast<double>(cross[1]) - by),
                          static_cast<float>(cross[2] - bz)};
        Virtual<std::uint32_t>(placed, slot::kPlace, static_cast<float*>(place), static_cast<float*>(heading));
        Virtual<std::uint32_t>(placed, slot::kSlot180, static_cast<float*>(heading));
        before = placed;
        if (++car == At<char**>(self, trailer::kCars + 4))
            return;
    }
}

// 0x00524180, the bus's slot 164 (seconds), ret 4: without the flags 0x20
// and 4 the base's (0x0054E3D0, AL 1). Else, while the current state's timer
// (+0x10) is above the seconds, the timer runs down and on half the draws of
// the core's generator (below 0x7FFFFFFF) the occupants of the 16-metre
// square about the position (the network service's slot 0x94, its list in
// the argument's slot, kind 0x496E5783) are asked for 0xA9B40F05 until one
// in the state 4 is removed (slot 13, false), each answer released through
// the element. Without one (or without an answer from the service) slot 53.
// At the end of the timer the base's (its EAX). AL 1 otherwise.
std::uint32_t __fastcall BusDriveFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("BusDriveFrame", logged);
    if (!Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, 0x20u)) &&
        !Bool(Virtual<std::uint32_t>(self, slot::kCheckFlag, 4u))) {
        CallThis<std::uint32_t>(addr::cSC4AutomatonBase_DriveFrame, self, seconds);
        return 1;
    }
    char* const state = At<char**>(self, field::kStatesEnd)[-1];
    float& timer = At<float>(state, 0x10);
    if (!(timer > seconds))
        return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_DriveFrame, self, seconds);
    timer = static_cast<float>(static_cast<double>(timer) - seconds);
    if (CallThis<std::uint32_t>(addr::cRZRandomSuper_Next, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random)) >=
        0x7FFFFFFFu)
        return 1;
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
    char* list = head;
    const double reach = Global<float>(addr::cSC4Bus_Reach);
    void* const network = Global<void*>(addr::SC4_NetworkService);
    std::uint32_t low[3], high[3];
    const float lowX = static_cast<float>(At<float>(self, field::kPosition) - reach);
    const float lowZ = static_cast<float>(At<float>(self, field::kPosition + 8) - reach);
    const float highX = static_cast<float>(reach + At<float>(self, field::kPosition));
    const float highZ = static_cast<float>(reach + At<float>(self, field::kPosition + 8));
    std::memcpy(&low[0], &lowX, 4);
    low[1] = 0xFF7FFFFFu;
    std::memcpy(&low[2], &lowZ, 4);
    std::memcpy(&high[0], &highX, 4);
    high[1] = 0x7F7FFFFFu;
    std::memcpy(&high[2], &highZ, 4);
    if (Bool(Virtual<std::uint32_t>(network, 0x94, static_cast<void*>(&list), static_cast<void*>(low),
                                    static_cast<void*>(high), 0x496E5783u, 0xFFFFFFFFu))) {
        bool removed = false;
        void* answer = nullptr;
        for (char* node = At<char*>(list, 0); node != list && !removed; node = At<char*>(node, 0)) {
            void* const object = At<void*>(node, 8);
            if (!Bool(Virtual<std::uint32_t>(object, 0x00, 0xA9B40F05u, &answer)))
                continue;
            if (Virtual<std::uint32_t>(answer, slot::kStateType) == 4u) {
                Virtual<std::uint32_t>(answer, slot::kRemove, 0u);
                removed = true;
            }
            Release(At<void*>(node, 8));
        }
        Virtual<std::uint32_t>(network, 0xC0, static_cast<void*>(&list));
        if (!removed)
            Virtual<std::uint32_t>(self, slot::kPopState);
    } else {
        Virtual<std::uint32_t>(self, slot::kPopState);
    }
    CallThis<void>(addr::SC4List_Destroy2, static_cast<void*>(&list));
    return 1;
}

// 0x005240D0, the bus's slot 172 (a, b, c, d), ret 0x10: with c, c's slot 48,
// that one's slot 3, and its slot 22 (0x1301) true and slot 173 (a, b) true,
// the state 0x1E pushed (slot 52: 0x1E, 0, 5000.0, four zeros) and AL 1.
// Else the base's slot 172 (0x0054D830, its EAX).
std::uint32_t __fastcall BusSlot172(char* self, void*, std::uint32_t a, std::uint32_t b, void* c, std::uint32_t d)
{
    static bool logged;
    FirstCall("BusSlot172", logged);
    if (c) {
        if (void* const next = Virtual<void*>(c, slot::kNextAutomaton)) {
            void* const part = Virtual<void*>(next, 0x0C);
            if (Bool(Virtual<std::uint32_t>(part, 0x58, 0x1301u)) && Bool(Virtual<std::uint32_t>(self, slot::kSlot173, a, b))) {
                Virtual<std::uint32_t>(self, slot::kPushState, 0x1Eu, 0u, 0x459C4000u, 0u, 0u, 0u, 0u);
                return 1;
            }
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonBase_Slot172, self, a, b, c, d);
}

// 0x00524440, the carjacked vehicle's slot 101: false when the core's slot
// 101 (0x0050CE60) is; else slot 184, the sound 0x00B09AB0 (slot 22: the
// key, 1, 1, 5.0) and slot 61 (0xCBC17DDF). AL only.
std::uint32_t __fastcall CarjackedSlot101(char* self, void*)
{
    static bool logged;
    FirstCall("CarjackedSlot101", logged);
    if (!Bool(CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Slot101, self)))
        return 0;
    Virtual<std::uint32_t>(self, slot::kSlot184);
    Virtual<std::uint32_t>(self, slot::kSound, reinterpret_cast<const void*>(addr::cSC4CarjackedVehicle_SoundKey), 1u,
                           1u, 5.0f);
    Virtual<std::uint32_t>(self, slot::kSlot61, 0xCBC17DDFu);
    return 1;
}

// 0x005339B0, the getaway van's slot 101: false when the core's slot 101
// (0x0050CE60) is. Else the siren (0x5AD0E817, 0xBADB57F1, and 0x117C0000
// loud, 0x117B0000 not) after slot 184 (slot 22: the key, 1, 1, 5.0). Loud
// when it is the automaton city slot 49's slot 9 names (its slot 5); else,
// with the police simulator and 0x00B43D70 and their slot 5 maps, at the
// position's cell (x 0.0625, __ftol2) in the city (city slot 99) unless the
// police map's byte there is above 0 and above the other's; else on half
// the draws of the core's generator. AL only.
std::uint32_t __fastcall VanSlot101(char* self, void*)
{
    static bool logged;
    FirstCall("VanSlot101", logged);
    if (!Bool(CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Slot101, self)))
        return 0;
    bool loud = false;
    bool decided = false;
    void* const driven = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
    if (driven) {
        const std::uint32_t id = Virtual<std::uint32_t>(driven, 0x24);
        if (id == Virtual<std::uint32_t>(self, slot::kId)) {
            loud = true;
            decided = true;
        }
    }
    if (!decided) {
        void* const police = Global<void*>(addr::SC4_PoliceSimulator);
        void* const other = Global<void*>(addr::SC4_Service43D70);
        if (police && other) {
            void* const policeMap = Virtual<void*>(police, 0x14);
            void* const otherMap = Virtual<void*>(other, 0x14);
            const double scale = Global<float>(addr::cSC4GetawayVan_CellScale);
            const std::int32_t x = Ftol(At<float>(self, field::kPosition) * scale);
            const std::int32_t z = Ftol(At<float>(self, field::kPosition + 8) * scale);
            if (policeMap && otherMap && Bool(Virtual<std::uint32_t>(Global<void*>(addr::SC4_City), 0x18C, x, z))) {
                const std::uint8_t p = Virtual<std::uint8_t>(policeMap, 0x1C, x, z);
                const std::uint8_t q = Virtual<std::uint8_t>(otherMap, 0x1C, x, z);
                loud = !(p > 0 && p > q);
                decided = true;
            }
        }
    }
    if (!decided)
        loud = CallThis<std::uint32_t>(addr::cRZRandomSuper_Next, reinterpret_cast<void*>(addr::cSC4AutomatonCore_Random)) <
               0x7FFFFFFFu;
    const std::uint32_t key[3] = {0x5AD0E817u, 0xBADB57F1u, loud ? 0x117C0000u : 0x117B0000u};
    Virtual<std::uint32_t>(self, slot::kSlot184);
    Virtual<std::uint32_t>(self, slot::kSound, static_cast<const void*>(key), 1u, 1u, 5.0f);
    return 1;
}

// 0x0053CD30, the limo's slot 121 (controls, seconds), ret 8: cSC4Vehicle's
// (0x0055C600, its answer dropped); with the controls' +0x98 above 1000, 50
// paid from a budget above 50 (slots 6 and 8) and 0x00B43DC4's slots 43
// (50), 50 (the position) and 44 (1), else the core's 0x0050D870 and the
// controls' byte +0x20C cleared; then 0x00B43D60's slot 7 (x, z, 48.0,
// 10.0) and +0x98 zeroed. Its own +0x2B8 to +0x2C0 (Init's) are not read.
// AL 1.
std::uint32_t __fastcall LimoSteer(char* self, void*, char* controls, float seconds)
{
    static bool logged;
    FirstCall("LimoSteer", logged);
    CallThis<std::uint32_t>(addr::cSC4Vehicle_Steer, self, controls, seconds);
    if (!(At<float>(controls, 0x98) > Global<float>(addr::cSC4AutomatonBase_Thousand)))
        return 1;
    bool paid = false;
    if (void* const budget = Global<void*>(addr::SC4_BudgetSimulator)) {
        if (Virtual<std::int64_t>(budget, 0x18) > 50) {
            Virtual<std::uint32_t>(budget, 0x20, 50u, 0u);
            if (void* const service = Global<void*>(addr::SC4_Service43DC4)) {
                Virtual<std::uint32_t>(service, 0xAC, 50u);
                Virtual<std::uint32_t>(service, 0xC8, self + field::kPosition);
                Virtual<std::uint32_t>(service, 0xB0, 1u);
            }
            paid = true;
        }
    }
    if (!paid) {
        CallThis<void>(addr::cSC4AutomatonCore_ResetStateAttachments, self);
        At<std::uint8_t>(controls, 0x20C) = 0;
    }
    if (void* const sounds = Global<void*>(addr::SC4_Service43D60))
        Virtual<std::uint32_t>(sounds, 0x1C, At<float>(self, field::kPosition), At<float>(self, field::kPosition + 8),
                               48.0f, 10.0f);
    At<std::uint32_t>(controls, 0x98) = 0;
    return 1;
}

// 0x0054A750, the tank's slot 127: the core's (0x00508D00), then both tank
// highlights off (slots 193 and 196, the latter a tail jump: its EAX).
std::uint32_t __fastcall TankRemoveHighlights(char* self, void*)
{
    static bool logged;
    FirstCall("TankRemoveHighlights", logged);
    CallThis<void>(addr::cSC4AutomatonCore_RemoveHighlight, self);
    Virtual<std::uint32_t>(self, slot::kRemoveHighlight1);
    return Virtual<std::uint32_t>(self, slot::kRemoveHighlight2);
}

// ====================================================================================
// The tank on frames (ro3a)

float TankZero() { return Global<float>(addr::cSC4Tank_Zero); }
float TankTwoPi() { return Global<float>(addr::cSC4Tank_TwoPi); }
float MinusOne() { return Global<float>(addr::cSC4AutomatonBase_MinusOne); }

// An x87 value as the FPU keeps it, its ten bytes: FSIN and FCOS leave 64
// bits of mantissa whatever the precision control, and slot 115 multiplies
// them as they are (each product then rounded to the 53 bits the precision
// control keeps).
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

using opensc4::x87::CrtAcos;

void UDriveSound(std::size_t on, std::uint32_t id)
{
    void* const sounds = CallCdecl<void*>(addr::SC4UDrive_GetService602640);
    Virtual<std::uint32_t>(sounds, on, id);
}

// 0x0054A780, the tank's slot 110 (state), ret 4: cSC4Vehicle's
// (0x0055B6D0, which reads the state's type without a null test); then for
// the state 0x10 with the controls (slot 56) the turret's record set: +0x248
// 0, +0x24C 0.4, +0x250 the barrel, +0x254 0, +0x258 2pi, +0x260 30.0,
// +0x264 100.0, +0x25C 2pi less the turret (0x00A9B7E0, loaded before the
// stores), +0x268 the velocity, and +0x144 12, +0x178 0, +0x114 0xAC0ECE77,
// +0x1AC 2000.0. AL the base's.
std::uint32_t __fastcall TankEnterState(char* self, void*, char* entered)
{
    static bool logged;
    FirstCall("TankEnterState", logged);
    const std::uint8_t answer = Bool(CallThis<std::uint32_t>(addr::cSC4Vehicle_EnterState, self, entered));
    if (!entered || At<std::uint32_t>(entered, 4) != 0x10)
        return answer;
    char* const c = Virtual<char*>(self, slot::kControls);
    if (!c)
        return answer;
    const double twoPi = TankTwoPi();
    At<std::uint32_t>(c, udrive::kRecord) = 0;
    At<std::uint32_t>(c, udrive::kRecord + 4) = 0x3ECCCCCDu;
    At<std::uint32_t>(c, udrive::kRecordBarrel) = At<std::uint32_t>(self, tank::kBarrel);
    At<std::uint32_t>(c, udrive::kRecord + 0xC) = 0;
    At<std::uint32_t>(c, udrive::kRecord + 0x10) = 0x40C90FDBu;
    At<std::uint32_t>(c, udrive::kRecord + 0x18) = 0x41F00000u;
    At<std::uint32_t>(c, udrive::kRecord + 0x1C) = 0x42C80000u;
    At<float>(c, udrive::kRecordTurn) = static_cast<float>(twoPi - At<float>(self, tank::kTurret));
    At<std::uint32_t>(c, udrive::kRecordVelocity) = At<std::uint32_t>(self, tank::kVelocity);
    At<std::uint32_t>(c, udrive::kWord144) = 0xC;
    At<std::uint32_t>(c, udrive::kShots) = 0;
    At<std::uint32_t>(c, udrive::kWord114) = 0xAC0ECE77u;
    At<std::uint32_t>(c, udrive::kWord1AC) = 0x44FA0000u;
    return answer;
}

// 0x0054AAE0, the tank's slot 115 (seconds), ret 4: cSC4Vehicle's
// (0x005603F0); with the controls (slot 56) whose mouse point +0x7C and
// +0x80 are unset (-1), and the occupant +0x1EC null or the controls' byte
// +0x228 set: the template's entry (slot 67, 0x0046B4A0 on it; AL 0
// without either) gives the rate +0x58. A turn key (+0x84, else +0x88
// negated; zeroed when used) turns the heading about its cross with (0, 1,
// 0) (k = 0x00A9B7A0, zero, multiplied through) by rate x key x -0.5 (the
// quaternion's half angle: FSIN and FCOS at the x87's full width), with the
// U-Drive sound 0x13 on (0x00602640 slot 5). Then the front +0x264 half the
// radius (slot 17) ahead, the controls' +0x44 half the radius behind and
// their +0x38 and +0x50 the heading, slot 16 (the position, the heading)
// and slot 100 (0, 1). Without a turn key, or with the mouse point set, the
// sound 0x13 off (slot 6). AL the base's but for the 0 above.
//
// The x87 at 53 bits: the axis rounded to floats, its length and the
// quaternion's parts as the game keeps them (x and y floats, z and w
// unrounded, the matrix's entries floats where the game stores them).
std::uint32_t __fastcall TankDriveFrame(char* self, void*, float seconds)
{
    static bool logged;
    FirstCall("TankDriveFrame", logged);
    const std::uint8_t driven = Bool(CallThis<std::uint32_t>(addr::cSC4Vehicle_DriveFrame, self, seconds));
    char* const c = Virtual<char*>(self, slot::kControls);
    if (!c)
        return driven;
    const float unset = MinusOne();
    // (The game asks +0x7C and +0x80 for -1 a second time here: as both are,
    // its branch that would clear the turn keys is never taken.)
    if (At<float>(c, udrive::kMouseX) != unset || At<float>(c, udrive::kMouseY) != unset) {
        UDriveSound(slot::kSoundOff, 0x13);
        return driven;
    }
    if (At<void*>(self, field::kOccupant) && !At<std::uint8_t>(c, udrive::kByte228))
        return driven;
    char* const owner = Virtual<char*>(self, slot::kTemplate);
    if (!owner)
        return 0;
    char* const entry = CallThis<char*>(addr::SC4AutomatonTemplate_FindInherited64, owner);
    if (!entry)
        return 0;
    float* const h = &At<float>(self, field::kHeading);
    const double k = TankZero();
    const double rate = At<float>(entry, 0x58);
    const float axis0 = static_cast<float>(static_cast<double>(h[2]) * k - static_cast<double>(h[0]) * h[1]);
    const float axis1 = static_cast<float>(static_cast<double>(h[0]) * h[0] - static_cast<double>(-h[2]) * h[2]);
    const float axis2 = static_cast<float>(static_cast<double>(-h[2]) * h[1] - static_cast<double>(h[0]) * k);
    double turn;
    if (At<float>(c, udrive::kTurnLeft) != unset) {
        turn = rate * At<float>(c, udrive::kTurnLeft);
        At<std::uint32_t>(c, udrive::kTurnLeft) = 0;
    } else if (At<float>(c, udrive::kTurnRight) != unset) {
        turn = rate * At<float>(c, udrive::kTurnRight) * Global<float>(addr::SC4_FloatMinusOne);
        At<std::uint32_t>(c, udrive::kTurnRight) = 0;
    } else {
        UDriveSound(slot::kSoundOff, 0x13);
        return driven;
    }
    const float angle = static_cast<float>(turn);
    UDriveSound(slot::kSoundOn, 0x13);
    const double half = static_cast<double>(angle) * Global<float>(addr::SC4_FloatMinusHalf);
    const double length = std::sqrt((static_cast<double>(axis2) * axis2 + static_cast<double>(axis1) * axis1) +
                                    static_cast<double>(axis0) * axis0);
    const double inverse = Global<float>(addr::SC4_FloatOneB0A078) / length;
    const float n0 = static_cast<float>(axis0 * inverse);
    const float n1 = static_cast<float>(axis1 * inverse);
    const float n2 = static_cast<float>(axis2 * inverse);
    X87 sine, cosine;
    X87SinCos(half, &sine, &cosine);
    // The quaternion (x, y, z, w) and its matrix's entries.
    const float x = static_cast<float>(X87Times(sine, n0));
    const float y = static_cast<float>(X87Times(sine, n1));
    const double z = X87Times(sine, n2);
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    const double yy = static_cast<double>(y) * y;
    const double xx = static_cast<double>(x) * x;
    const double zz = z * z;
    const float zz2 = static_cast<float>(zz + zz);
    const double xx1 = one - (xx + xx);
    const float m70 = static_cast<float>(xx1 - (yy + yy));
    const float m60 = static_cast<float>(xx1 - zz2);
    const float m50 = static_cast<float>((one - (yy + yy)) - zz2);
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
    const double a = static_cast<double>(xy2) + zw2;
    const float m58 = static_cast<float>(static_cast<double>(xz2) - yw2);
    const double b = static_cast<double>(xy2) - zw2;
    const double cc = static_cast<double>(yz2) + xw2;
    const double d = static_cast<double>(yw2) + xz2;
    const double e = static_cast<double>(yz2) - xw2;
    // The rows, each entry plus k times the two others it was folded with.
    const float bk = static_cast<float>(b * k);
    const double m50k = m50 * k;
    const float r2c = static_cast<float>((d + m50k) + bk);
    const float ak = static_cast<float>(a * k);
    const float m60k = static_cast<float>(m60 * k);
    const float r30 = static_cast<float>((e + m60k) + ak);
    const float ck = static_cast<float>(cc * k);
    const float m58k = static_cast<float>(m58 * k);
    const float r34 = static_cast<float>((static_cast<double>(m70) + m58k) + ck);
    const float dk = static_cast<float>(d * k);
    const float r38 = static_cast<float>((b + dk) + m50k);
    const float ek = static_cast<float>(e * k);
    const float r3c = static_cast<float>((static_cast<double>(m60) + ek) + ak);
    const float m70k = static_cast<float>(m70 * k);
    const float r40 = static_cast<float>((cc + m70k) + m58k);
    const float r44 = static_cast<float>((static_cast<double>(m50) + dk) + bk);
    const double p = (a + ek) + m60k;
    const double q = (static_cast<double>(m58) + m70k) + ck;
    const double t0 = (q * h[2] + static_cast<double>(r44) * h[0]) + p * h[1];
    const double t1 = (static_cast<double>(r40) * h[2] + static_cast<double>(r38) * h[0]) + static_cast<double>(r3c) * h[1];
    const double t2 = (static_cast<double>(r2c) * h[0] + static_cast<double>(r34) * h[2]) + static_cast<double>(r30) * h[1];
    h[2] = static_cast<float>(t2);
    h[0] = static_cast<float>(t0);
    h[1] = static_cast<float>(t1);
    // The front half the radius ahead, the controls' point half behind; x by
    // the unrounded product, y and z by floats.
    float* const position = &At<float>(self, field::kPosition);
    const double halfOne = static_cast<double>(Global<float>(addr::SC4_FloatOneB0A078)) * Global<float>(addr::SC4_FloatHalf);
    const float halfOneF = static_cast<float>(halfOne);
    {
        const double radius = Virtual<double>(self, slot::kGetRadius);
        const double ahead0 = radius * h[0] * halfOne;
        const float ahead1 = static_cast<float>(static_cast<float>(radius * h[1]) * static_cast<double>(halfOneF));
        const float ahead2 = static_cast<float>(static_cast<float>(radius * h[2]) * static_cast<double>(halfOneF));
        float* const front = &At<float>(self, field::kFront);
        front[0] = static_cast<float>(ahead0 + position[0]);
        front[1] = static_cast<float>(static_cast<double>(ahead1) + position[1]);
        front[2] = static_cast<float>(static_cast<double>(ahead2) + position[2]);
    }
    {
        const double radius = Virtual<double>(self, slot::kGetRadius);
        const double behind0 = radius * h[0] * halfOne;
        const float behind1 = static_cast<float>(static_cast<float>(radius * h[1]) * static_cast<double>(halfOneF));
        const float behind2 = static_cast<float>(static_cast<float>(radius * h[2]) * static_cast<double>(halfOneF));
        float* const back = &At<float>(c, udrive::kBack);
        back[2] = static_cast<float>(static_cast<double>(position[2]) - behind2);
        back[0] = static_cast<float>(position[0] - behind0);
        back[1] = static_cast<float>(static_cast<double>(position[1]) - behind1);
    }
    std::memcpy(c + udrive::kHeading1, h, 12);
    std::memcpy(c + udrive::kHeading2, h, 12);
    Virtual<std::uint32_t>(self, slot::kPlace, position, h);
    Virtual<std::uint32_t>(self, slot::kSlot100, 0u, 1u);
    return driven;
}

// The velocity +0x354 clamped to 30..100 (0x00A9B7C0, 0x00A9B7C4) as the
// game stores it: the value's float, or the bound's.
void ClampVelocity(char* self, double value)
{
    const float least = Global<float>(addr::cSC4Tank_VelocityLeast);
    const float most = Global<float>(addr::cSC4Tank_VelocityMost);
    At<float>(self, tank::kVelocity) = value < least ? least : value > most ? most : static_cast<float>(value);
}

// The barrel angle +0x350 clamped to 0..0.4 (0x00A9B7A0, 0x00A9B7A4).
void ClampBarrel(char* self, double value)
{
    const float least = TankZero();
    const float most = Global<float>(addr::cSC4Tank_BarrelMost);
    At<float>(self, tank::kBarrel) = value < least ? least : value > most ? most : static_cast<float>(value);
}

// A key of the controls, held (not below 0) and moved past its value
// before: by how much, else 0 (the game's FCOM / test ah,0x41: above,
// ordered).
bool KeyMoved(char* controls, std::size_t now, std::size_t before, double& by)
{
    if (At<float>(controls, now) < TankZero() || At<float>(controls, now) != At<float>(controls, now))
        return false;
    by = static_cast<double>(At<float>(controls, now)) - At<float>(controls, before);
    return by > TankZero();
}

// A sound timer of the game's (0x00B228FC, 0x00B228F8): past 50 (0x00B0A074)
// it restarts and the sound `id` goes on (0x00602640 slot 5) when the angle
// moved more than `least` since the frame began, else off (slot 6); else it
// runs on by the seconds.
void SoundTimer(std::uintptr_t timer, float now, float before, std::uintptr_t least, std::uint32_t id, float seconds)
{
    float& t = *reinterpret_cast<float*>(timer);
    if (t > Global<float>(addr::cSC4Tank_SoundTime)) {
        t = 0.0f;
        const bool moved = std::fabs(static_cast<double>(now) - before) > Global<float>(least);
        UDriveSound(moved ? slot::kSoundOn : slot::kSoundOff, id);
    } else {
        t = static_cast<float>(static_cast<double>(t) + seconds);
    }
}

// The barrel model's transform as its virtual base's slot 9 fills it: two
// bytes (the game's two after them are stack garbage; ours zero), a matrix
// by rows, a place and a scale.
struct TankTransform {
    std::uint8_t flags[4];
    float matrix[9];
    float place[3];
    float scale;
};

// 0x0054B030, the tank's slot 121 (controls, seconds), ret 8: the byte
// +0x340 set; with slot 18 only cSC4Vehicle's (0x0055C600, its EAX). Else:
//  - the turret +0x34C brought into 0..2pi (2pi taken or added while past);
//  - the key +0xA0 at 250 or more (0x00A954C0): zeroed, the velocity +0x354
//    up 5, 30 when that passes 100, then clamped to 30..100;
//  - with +0x9C not below 0: the pointer (+0x214, +0x218 as floats, then
//    __ftol2) picked by 0x00B43DC4's slot 36 (no null test) into a point
//    filled from 0x00B22920; on an answer the turret turns towards it by
//    at most the static rate 0x00B22900 (0.12 degrees on first use, guard
//    0x00B22904) x seconds, the short way: the angle from __CIacos of the
//    flat unit vectors' dot, 2pi less it when their cross is above 0;
//  - the fire key +0x98 not below 0 with the barrel model +0x360 and the
//    controls' byte +0x1FF: +0x1E0 and the byte cleared, +0x178 counted,
//    the attachments +0x368 placed (slot 4) at the barrel model's place (its
//    virtual base's slot 9) with the heading and +0x94, and the shot +0x364
//    (no null test) given the heading times the speed (0x004FA960 on
//    +0x1A0; slot 11 with 0xF), the velocity x 0.01 (slot 12) and started
//    (slot 3);
//  - the keys +0x90 and +0x94 (moved past +0xC4 and +0xC8) the barrel up
//    and down by 0.00025 each (clamped to 0..0.4), +0xA8 and +0xAC (past
//    +0xDC and +0xE0) the velocity by 0.02 (clamped to 30..100);
//  - the two sound timers (0x0C the turret's over 0.1 degrees, 0x0F the
//    barrel's over 0.004);
//  - the mouse point +0x344 from the pointer, the controls' record barrel,
//    2pi less the turret and velocity; then cSC4Vehicle's (its EAX).
std::uint32_t __fastcall TankSteer(char* self, void*, char* controls, float seconds)
{
    static bool logged;
    FirstCall("TankSteer", logged);
    At<std::uint8_t>(self, tank::kFiring) = 1;
    const float pointerX = static_cast<float>(At<std::int32_t>(controls, udrive::kPointerX));
    const float pointerY = static_cast<float>(At<std::int32_t>(controls, udrive::kPointerY));
    if (Bool(Virtual<std::uint32_t>(self, slot::kSlot18)))
        return CallThis<std::uint32_t>(addr::cSC4Vehicle_Steer, self, controls, seconds);
    const double twoPi = TankTwoPi();
    const double zero = TankZero();
    float& turret = At<float>(self, tank::kTurret);
    if (turret > twoPi) {
        double t = turret;
        do
            t -= twoPi;
        while (t > twoPi);
        turret = static_cast<float>(t);
    }
    if (turret < zero) {
        double t = turret;
        do
            t += twoPi;
        while (t < zero);
        turret = static_cast<float>(t);
    }
    const float barrelBefore = At<float>(self, tank::kBarrel);
    const float turretBefore = turret;
    float& velocity = At<float>(self, tank::kVelocity);
    if (At<float>(controls, udrive::kFaster) >= Global<float>(addr::cSC4Tank_FasterHeld)) {
        At<std::uint32_t>(controls, udrive::kFaster) = 0;
        const double faster = static_cast<double>(velocity) + Global<float>(addr::SC4_FloatFive);
        velocity = static_cast<float>(faster);
        if (faster > Global<float>(addr::cSC4Tank_VelocityMost))
            velocity = 30.0f; // 0x41F00000, a literal of the game's
        ClampVelocity(self, velocity);
    }
    if (At<float>(controls, udrive::kTurretKey) >= zero) {
        float point[3];
        const std::uint32_t fill = Global<std::uint32_t>(addr::cSC4Tank_FillValue);
        std::memcpy(point + 0, &fill, 4);
        std::memcpy(point + 1, &fill, 4);
        std::memcpy(point + 2, &fill, 4);
        std::uint32_t& guard = *reinterpret_cast<std::uint32_t*>(addr::cSC4Tank_TurretRateGuard);
        if (!(guard & 1)) {
            guard |= 1;
            *reinterpret_cast<std::uint32_t*>(addr::cSC4Tank_TurretRate) = 0x3B09421Eu;
        }
        void* const picker = Global<void*>(addr::SC4_Service43DC4);
        const std::int32_t y = Ftol(pointerY);
        const std::int32_t x = Ftol(pointerX);
        if (Bool(Virtual<std::uint32_t>(picker, slot::kPick, x, y, static_cast<float*>(point), 0u))) {
            const float* const position = &At<float>(self, field::kPosition);
            const double one = Global<float>(addr::SC4_FloatOneB0A078);
            const double dx = static_cast<double>(point[0]) - position[0];
            const double dz = static_cast<double>(point[2]) - position[2];
            const double inverse = one / std::sqrt(dz * dz + dx * dx);
            point[0] = static_cast<float>(dx * inverse);
            point[1] = static_cast<float>(zero * inverse);
            point[2] = static_cast<float>(dz * inverse);
            const float* const h = &At<float>(self, field::kHeading);
            const double inverse2 = one / std::sqrt(static_cast<double>(h[0]) * h[0] + static_cast<double>(h[2]) * h[2]);
            const float e0 = static_cast<float>(inverse2 * h[0]);
            const float e1 = static_cast<float>(zero * inverse2);
            const float e2 = static_cast<float>(h[2] * inverse2);
            float angle = CrtAcos((static_cast<double>(e0) * point[0] + static_cast<double>(e2) * point[2]) +
                                  static_cast<double>(e1) * point[1]);
            if (static_cast<double>(point[2]) * e0 - static_cast<double>(e2) * point[0] > zero)
                angle = static_cast<float>(twoPi - angle);
            const double diff = static_cast<double>(angle) - turret;
            const float diffF = static_cast<float>(diff);
            const double step = static_cast<double>(Global<float>(addr::cSC4Tank_TurretRate)) * seconds;
            const float stepF = static_cast<float>(step);
            const float backF = static_cast<float>(-step);
            double by;
            if (-step > diffF)
                by = backF;
            else if (diff > stepF)
                by = stepF;
            else
                by = diff;
            const double way = by > zero ? static_cast<double>(diffF) : static_cast<double>(turret) - angle;
            if (way > Global<float>(addr::cSC4Tank_Pi))
                turret = static_cast<float>(turret - by);
            else
                turret = static_cast<float>(by + turret);
        }
    }
    if (At<float>(controls, udrive::kFire) >= zero && At<void*>(self, tank::kBarrelModel) &&
        At<std::uint8_t>(controls, udrive::kFireByte)) {
        At<std::uint32_t>(controls, udrive::kWord1E0) = 0;
        At<std::uint8_t>(controls, udrive::kFireByte) = 0;
        ++At<std::int32_t>(controls, udrive::kShots);
        float speed = static_cast<float>(static_cast<double>(velocity) * Global<float>(addr::cSC4Tank_VelocityScale));
        TankTransform t = {{0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 1.0f};
        char* const model = At<char*>(self, tank::kBarrelModel);
        char* const base = model + 4 + At<std::int32_t>(At<char*>(model, 4), 4);
        Virtual<std::uint32_t>(base, slot::kModelTransform, &t);
        float place[3];
        std::memcpy(place, t.place, sizeof place);
        float* const h = &At<float>(self, field::kHeading);
        Virtual<std::uint32_t>(self + tank::kAttachments, slot::kAttachPlace, static_cast<float*>(place), h,
                               At<std::uint32_t>(self, field::kWord94));
        const double now = CallThis<double>(addr::cSC4AutomatonCore_SmoothedValue, self + field::kSmoothed);
        float moving[3] = {static_cast<float>(now * h[0]), static_cast<float>(now * h[1]), static_cast<float>(now * h[2])};
        void* const shot = At<void*>(self, tank::kShot);
        Virtual<std::uint32_t>(shot, slot::kShotVelocity, 0xFu, static_cast<float*>(moving), 1u);
        Virtual<std::uint32_t>(At<void*>(self, tank::kShot), slot::kShotSpeed, 0u, &speed, 1u);
        Virtual<std::uint32_t>(At<void*>(self, tank::kShot), slot::kShotStart, 0u);
    }
    const double barrelRate = Global<float>(addr::cSC4Tank_BarrelRate);
    const double velocityRate = Global<float>(addr::cSC4Tank_VelocityRate);
    double by;
    if (KeyMoved(controls, udrive::kBarrelUp, udrive::kBarrelUpBefore, by)) {
        const double barrel = by * barrelRate + At<float>(self, tank::kBarrel);
        ClampBarrel(self, barrel);
    }
    if (KeyMoved(controls, udrive::kBarrelDown, udrive::kBarrelDownBefore, by)) {
        const double barrel = static_cast<double>(At<float>(self, tank::kBarrel)) - by * barrelRate;
        ClampBarrel(self, barrel);
    }
    if (KeyMoved(controls, udrive::kVelocityUp, udrive::kVelocityUpBefore, by))
        ClampVelocity(self, by * velocityRate + velocity);
    if (KeyMoved(controls, udrive::kVelocityDown, udrive::kVelocityDownBefore, by))
        ClampVelocity(self, static_cast<double>(velocity) - by * velocityRate);
    SoundTimer(addr::cSC4Tank_TurretSoundTimer, turret, turretBefore, addr::cSC4Tank_TurretSoundLeast, 0xC, seconds);
    SoundTimer(addr::cSC4Tank_BarrelSoundTimer, At<float>(self, tank::kBarrel), barrelBefore,
               addr::cSC4CitySituationManager_IntensityScale, 0xF, seconds);
    const std::uint32_t barrel = At<std::uint32_t>(self, tank::kBarrel);
    At<float>(self, tank::kMouse) = static_cast<float>(At<std::int32_t>(controls, udrive::kPointerX));
    At<float>(self, tank::kMouse + 4) = static_cast<float>(At<std::int32_t>(controls, udrive::kPointerY));
    At<std::uint32_t>(controls, udrive::kRecordBarrel) = barrel;
    At<float>(controls, udrive::kRecordTurn) = static_cast<float>(twoPi - turret);
    At<std::uint32_t>(controls, udrive::kRecordVelocity) = At<std::uint32_t>(self, tank::kVelocity);
    return CallThis<std::uint32_t>(addr::cSC4Vehicle_Steer, self, controls, seconds);
}

// ====================================================================================
// The tank's frame (ro3b)

// A turn's quaternion: x and y floats, z unrounded, the cosine at 64 bits.
struct TankTurn {
    float x, y;
    double z;
    X87 cosine;
};

// The quaternion of a turn by twice `half` about the unit axis n.
TankTurn TankQuaternion(float n0, float n1, float n2, double half)
{
    TankTurn t{};
    X87 sine;
    X87SinCos(half, &sine, &t.cosine);
    t.x = static_cast<float>(X87Times(sine, n0));
    t.y = static_cast<float>(X87Times(sine, n1));
    t.z = X87Times(sine, n2);
    return t;
}

// A turn's matrix as the game keeps it: a, b and c the diagonal's
// (1 - 2xx - 2yy, 1 - 2xx - 2zz, 1 - 2yy - 2zz), p = 2xy + 2zw, q = 2xz -
// 2yw, r = 2xy - 2zw, s = 2yz + 2xw, u = 2yw + 2xz, v = 2yz - 2xw. Each held
// as a double; which are floats depends on the instance.
struct TankMatrix {
    double a, b, c, p, q, r, s, u, v;
};

// The turret's two instances (the first and third turns): 2xx and 2yy
// unrounded, 2zz and the parts of p..v floats, p, r, s, u and v unrounded.
TankMatrix TankMatrixA(const TankTurn& t)
{
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    const double yy = static_cast<double>(t.y) * t.y;
    const double yy2 = yy + yy;
    const double xx = static_cast<double>(t.x) * t.x;
    const double xx2 = xx + xx;
    const double zz = t.z * t.z;
    const float zz2 = static_cast<float>(zz + zz);
    const double xx1 = one - xx2;
    TankMatrix m{};
    m.a = static_cast<float>(xx1 - yy2);
    m.b = static_cast<float>(xx1 - zz2);
    m.c = static_cast<float>((one - yy2) - zz2);
    const double zx = t.z * t.x;
    const float xz2 = static_cast<float>(zx + zx);
    const double yx = static_cast<double>(t.y) * t.x;
    const float xy2 = static_cast<float>(yx + yx);
    const double xw = X87Times(t.cosine, t.x);
    const float xw2 = static_cast<float>(xw + xw);
    const double zy = t.z * t.y;
    const float yz2 = static_cast<float>(zy + zy);
    const double yw = X87Times(t.cosine, t.y);
    const float yw2 = static_cast<float>(yw + yw);
    const double zw = X87Times(t.cosine, t.z);
    const double zw2 = zw + zw;
    m.p = xy2 + zw2;
    m.q = static_cast<float>(static_cast<double>(xz2) - yw2);
    m.r = xy2 - zw2;
    m.s = static_cast<double>(yz2) + xw2;
    m.u = static_cast<double>(yw2) + xz2;
    m.v = static_cast<double>(yz2) - xw2;
    return m;
}

// The barrel's two instances (the second and fourth turns): 2xx a float,
// 1 - 2xx unrounded for a and a float for b; p and r unrounded, the rest
// floats.
TankMatrix TankMatrixB(const TankTurn& t)
{
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    const double yy = static_cast<double>(t.y) * t.y;
    const double yy2 = yy + yy;
    const double xx = static_cast<double>(t.x) * t.x;
    const float xx2 = static_cast<float>(xx + xx);
    const double zz = t.z * t.z;
    const float zz2 = static_cast<float>(zz + zz);
    const double xx1 = one - xx2;
    const float xx1f = static_cast<float>(xx1);
    TankMatrix m{};
    m.a = static_cast<float>(xx1 - yy2);
    m.b = static_cast<float>(static_cast<double>(xx1f) - zz2);
    m.c = static_cast<float>((one - yy2) - zz2);
    const double xz = static_cast<double>(t.x) * t.z;
    const float xz2 = static_cast<float>(xz + xz);
    const double xy = static_cast<double>(t.x) * t.y;
    const float xy2 = static_cast<float>(xy + xy);
    const double xw = X87Times(t.cosine, t.x);
    const float xw2 = static_cast<float>(xw + xw);
    const double zy = t.z * t.y;
    const float yz2 = static_cast<float>(zy + zy);
    const double yw = X87Times(t.cosine, t.y);
    const float yw2 = static_cast<float>(yw + yw);
    const double zw = X87Times(t.cosine, t.z);
    const double zw2 = zw + zw;
    m.p = xy2 + zw2;
    m.q = static_cast<float>(static_cast<double>(xz2) - yw2);
    m.r = xy2 - zw2;
    m.s = static_cast<float>(static_cast<double>(yz2) + xw2);
    m.u = static_cast<float>(static_cast<double>(yw2) + xz2);
    m.v = static_cast<float>(static_cast<double>(yz2) - xw2);
    return m;
}

// The turret's matrix folded with the zero k (0x00A9B7A0) in its rows: nine
// entries, the first seven floats, the last two unrounded.
struct TankRows {
    double e[9];
};

// The first instance: some k products unrounded where the game keeps them
// on the stack.
TankRows TankRowsFirst(const TankMatrix& m, double k)
{
    TankRows r{};
    const float rk = static_cast<float>(m.r * k);
    const double ck = m.c * k;
    const float ckf = static_cast<float>(ck);
    r.e[0] = static_cast<float>((ck + rk) + m.u);
    const float pk = static_cast<float>(m.p * k);
    const double bk = m.b * k;
    const float bkf = static_cast<float>(bk);
    r.e[1] = static_cast<float>((bk + pk) + m.v);
    const float sk = static_cast<float>(m.s * k);
    const double qk = m.q * k;
    const float qkf = static_cast<float>(qk);
    r.e[2] = static_cast<float>((qk + sk) + m.a);
    const double uk = m.u * k;
    const float ukf = static_cast<float>(uk);
    r.e[3] = static_cast<float>((uk + ckf) + m.r);
    const double vk = m.v * k;
    const float vkf = static_cast<float>(vk);
    r.e[4] = static_cast<float>((vk + pk) + m.b);
    const double ak = m.a * k;
    const float akf = static_cast<float>(ak);
    r.e[5] = static_cast<float>((ak + qkf) + m.s);
    r.e[6] = static_cast<float>((static_cast<double>(ukf) + rk) + m.c);
    r.e[7] = (static_cast<double>(vkf) + bkf) + m.p;
    r.e[8] = (static_cast<double>(akf) + sk) + m.q;
    return r;
}

// The third instance: every k product a float but b's, the entry first.
TankRows TankRowsSecond(const TankMatrix& m, double k)
{
    TankRows r{};
    const float rk = static_cast<float>(m.r * k);
    const float ck = static_cast<float>(m.c * k);
    r.e[0] = static_cast<float>((m.u + ck) + rk);
    const float pk = static_cast<float>(m.p * k);
    const double bk = m.b * k;
    const float bkf = static_cast<float>(bk);
    r.e[1] = static_cast<float>((bk + m.v) + pk);
    const float sk = static_cast<float>(m.s * k);
    const float qk = static_cast<float>(m.q * k);
    r.e[2] = static_cast<float>((m.a + qk) + sk);
    const float uk = static_cast<float>(m.u * k);
    r.e[3] = static_cast<float>((m.r + uk) + ck);
    const float vk = static_cast<float>(m.v * k);
    r.e[4] = static_cast<float>((m.b + vk) + pk);
    const float ak = static_cast<float>(m.a * k);
    r.e[5] = static_cast<float>((m.s + ak) + qk);
    r.e[6] = static_cast<float>((m.c + uk) + rk);
    r.e[7] = (m.p + vk) + bkf;
    r.e[8] = (m.q + ak) + sk;
    return r;
}

// The barrel's matrix times the turret's rows (both instances alike): the
// first eight floats, the last unrounded.
TankRows TankProduct(const TankMatrix& m, const TankRows& t)
{
    TankRows r{};
    for (int row = 0; row < 3; ++row) {
        const double* const e = t.e + 3 * row;
        r.e[3 * row] = static_cast<float>((m.c * e[0] + m.u * e[2]) + m.r * e[1]);
        r.e[3 * row + 1] = static_cast<float>((m.p * e[0] + m.v * e[2]) + m.b * e[1]);
        const double last = (m.q * e[0] + m.s * e[1]) + m.a * e[2];
        r.e[3 * row + 2] = row < 2 ? static_cast<float>(last) : last;
    }
    return r;
}

// A model's slot 2 (point, axis), ret 8.
void PlaceModel(void* model, float* point, float* axis)
{
    Virtual<std::uint32_t>(model, slot::kModelPlace, point, axis);
}

// 0x0054B7C0, the tank's slot 72 (word, seconds), ret 8: cSC4Vehicle's
// frame (0x0055BA20, its answer dropped). With the byte +0x340 set and both
// models (+0x35C, +0x360), the models placed (their slot 2: a point and
// the axis) by four quaternion turns: the axis is the heading's cross with
// (0, 1, 0) (k = 0x00A9B7A0, zero, multiplied through); the turret turns by
// the turret angle about it, the barrel by the barrel angle about (-z, 0, x)
// of the heading, and the point is the product's rows times the heading. The
// turret model's point has the barrel's turn at a quarter of the angle, the
// barrel model's at half; the second pair is made after the turret model's
// call from the axis, the angles and the heading read again (the barrel's
// axis is not). Then the attachments' slot 5 and the tank's slots 194 and
// 197 (the highlights' updates). AL 1 (the rest of EAX slot 197's).
//
// The x87 at 53 bits: each FSIN and FCOS at 64 bits in the products, the
// game's float stores where it makes them.
std::uint32_t __fastcall TankFrame(char* self, void*, std::uint32_t word, float seconds)
{
    static bool logged;
    FirstCall("TankFrame", logged);
    CallThis<std::uint32_t>(addr::cSC4Vehicle_Frame, self, word, seconds);
    void* const turretModel = At<void*>(self, tank::kTurretModel);
    if (At<std::uint8_t>(self, tank::kFiring) && turretModel && At<void*>(self, tank::kBarrelModel)) {
        const double k = TankZero();
        const double half = Global<float>(addr::SC4_FloatHalf);
        const double minusHalf = Global<float>(addr::SC4_FloatMinusHalf);
        const double oneA = Global<float>(addr::SC4_FloatOneA81228);
        const double oneB = Global<float>(addr::SC4_FloatOneB0A078);
        const float* const h = &At<float>(self, field::kHeading);
        const float h0 = h[0], h1 = h[1], h2 = h[2];
        const float minusH2 = -h2;
        // The axis: the heading's cross with (0, 1, 0).
        float axis[3];
        axis[0] = static_cast<float>(static_cast<double>(h2) * k - static_cast<double>(h0) * h1);
        axis[1] = static_cast<float>(static_cast<double>(h0) * h0 - static_cast<double>(minusH2) * h2);
        axis[2] = static_cast<float>(static_cast<double>(minusH2) * h1 - static_cast<double>(h0) * k);
        // The barrel's axis (-z, 0, x) over its length, the first time from
        // the unrounded inverse, the second from its float.
        const double flat = oneA / std::sqrt(static_cast<double>(h0) * h0 + static_cast<double>(minusH2) * minusH2);
        const float flatF = static_cast<float>(flat);
        float point[3];
        {
            const double inverse = oneB / std::sqrt((static_cast<double>(axis[2]) * axis[2] +
                                                     static_cast<double>(axis[1]) * axis[1]) +
                                                    static_cast<double>(axis[0]) * axis[0]);
            const TankTurn turret = TankQuaternion(static_cast<float>(axis[0] * inverse),
                                                   static_cast<float>(axis[1] * inverse),
                                                   static_cast<float>(axis[2] * inverse),
                                                   At<float>(self, tank::kTurret) * minusHalf);
            const TankRows rows = TankRowsFirst(TankMatrixA(turret), k);
            const double scale = flat * oneB;
            const TankTurn barrel = TankQuaternion(static_cast<float>(minusH2 * scale), static_cast<float>(k * scale),
                                                   static_cast<float>(scale * h0),
                                                   At<float>(self, tank::kBarrel) * half * minusHalf);
            const TankRows p = TankProduct(TankMatrixB(barrel), rows);
            point[0] = static_cast<float>((p.e[6] * h0 + p.e[8] * h2) + p.e[7] * h1);
            point[1] = static_cast<float>((p.e[3] * h0 + p.e[5] * h2) + p.e[4] * h1);
            point[2] = static_cast<float>((p.e[0] * h0 + p.e[2] * h2) + p.e[1] * h1);
            PlaceModel(turretModel, point, axis);
        }
        {
            const double inverse = oneB / std::sqrt((static_cast<double>(axis[0]) * axis[0] +
                                                     static_cast<double>(axis[2]) * axis[2]) +
                                                    static_cast<double>(axis[1]) * axis[1]);
            const TankTurn turret = TankQuaternion(static_cast<float>(axis[0] * inverse),
                                                   static_cast<float>(axis[1] * inverse),
                                                   static_cast<float>(inverse * axis[2]),
                                                   At<float>(self, tank::kTurret) * minusHalf);
            const TankRows rows = TankRowsSecond(TankMatrixA(turret), k);
            const double scale = flatF * oneB;
            const TankTurn barrel = TankQuaternion(static_cast<float>(minusH2 * scale), static_cast<float>(k * scale),
                                                   static_cast<float>(h0 * scale),
                                                   At<float>(self, tank::kBarrel) * minusHalf);
            const TankRows p = TankProduct(TankMatrixB(barrel), rows);
            const double g0 = h[0], g1 = h[1], g2 = h[2];
            point[0] = static_cast<float>((g0 * p.e[6] + p.e[8] * g2) + p.e[7] * g1);
            point[1] = static_cast<float>((p.e[5] * g2 + p.e[4] * g1) + g0 * p.e[3]);
            point[2] = static_cast<float>((p.e[2] * g2 + p.e[1] * g1) + g0 * p.e[0]);
            PlaceModel(At<void*>(self, tank::kBarrelModel), point, axis);
        }
    }
    Virtual<std::uint32_t>(self + tank::kAttachments, slot::kAttachDone);
    Virtual<std::uint32_t>(self, slot::kUpdateHighlight1);
    const std::uint32_t last = Virtual<std::uint32_t>(self, slot::kUpdateHighlight2);
    return (last & 0xFFFFFF00u) | 1u;
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

// Part ro2.
const Detour kFrame[] = {
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_Frame, TrailerFrame),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_CanMeet, TrailerCanMeet),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_SetTemplate, TrailerSetTemplate),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_EnterOccupant, TrailerEnterOccupant),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_PushWord, TrailerPushWord),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_AddCar, TrailerAddCar),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_FollowPath, TrailerFollowPath),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_FollowLeader, TrailerFollowLeader),
    OPENSC4_DETOUR(addr::cSC4TrailerVehicle_CrossPath, TrailerCrossPath),
    OPENSC4_DETOUR(addr::cSC4Bus_DriveFrame, BusDriveFrame),
    OPENSC4_DETOUR(addr::cSC4Bus_Slot172, BusSlot172),
    OPENSC4_DETOUR(addr::cSC4CarjackedVehicle_Slot101, CarjackedSlot101),
    OPENSC4_DETOUR(addr::cSC4GetawayVan_Slot101, VanSlot101),
    OPENSC4_DETOUR(addr::cSC4MayorLimo_Steer, LimoSteer),
    OPENSC4_DETOUR(addr::cSC4Tank_RemoveHighlights, TankRemoveHighlights),
};

// Part ro3a.
const Detour kTankFrame[] = {
    OPENSC4_DETOUR(addr::cSC4Tank_EnterState, TankEnterState),
    OPENSC4_DETOUR(addr::cSC4Tank_DriveFrame, TankDriveFrame),
    OPENSC4_DETOUR(addr::cSC4Tank_Steer, TankSteer),
    OPENSC4_DETOUR(addr::cSC4Tank_Frame, TankFrame), // part ro3b
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kFrame, sizeof kFrame / sizeof kFrame[0], "frame") &&
           InstallList(kTankFrame, sizeof kTankFrame / sizeof kTankFrame[0], "tank frame");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.roadveh", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4TrailerVehicle_Construct,
                        "Our other road vehicles (the trailer, the bus, the getaway van, the carjacked vehicle, the "
                        "tank and the mayor's limo), the objects: the six factories and constructors, the tank's and "
                        "the trailer's destructor bodies, the four deleting destructors, the trailer's "
                        "QueryInterface and the six GetGZCLSIDs; the five Writes and Reads; the Inits of the limo, the "
                        "tank and the trailer and the Shutdowns of the tank and the trailer; the trailer's rear point "
                        "and counts and the tank's debug text (44 detours, part ro1); the frames: the trailer's "
                        "slots 72, 73, 95, 132 and 192 to 198, the bus's 164 and 172, the carjacked vehicle's and the "
                        "van's 101, the limo's 121 and the tank's 127 (15 detours, part ro2); the tank on "
                        "frames: its slots 110, 115 and 121 (3 detours, part ro3a) and its frame, slot 72 (1 detour, "
                        "part ro3b; src/core/automata_roadveh.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_roadveh
