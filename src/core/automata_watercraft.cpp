// Our boat, part wc1 (build 641): cSC4Watercraft on the automata core
// (src/core/automata_vehicle.cpp has the core's ported slots; the core's own
// code stays the game's). wc1: the objects (the factory, the constructor,
// the destructor body and the deleting destructor, QueryInterface and
// GetGZCLSID), the save (Write and Read), the life cycle (Init and Shutdown,
// with the two dock-table helpers Shutdown calls) and the queries (slot 16,
// the placement, and slot 93, the debug text). The layout is in
// src/core/automata_watercraft.h and the survey in docs/automata.md, "Part
// wc". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.watercraft: the 14 detours in kObject (wc1) below.
//
// What it does. The boat is 0xDC8 bytes from SC4::operator_new and goes
// back through SC4::operator_delete. Its constructor runs the core's with
// four zero words, sets its six vectors of references and points, a path of
// 11 zeroed points, its own generator seeded with -1, the type 0x897F7EC2
// and the core's flag 0x10 at +0x33. Its save is the core's after a version
// word of 2. Init (once, while the occupant part is not initialised) runs
// the core's Init and the drive parameters' reader (0x00567310), makes its
// six effects through the city's effects manager, sets four automata flags,
// listens to the message 0xAC400A31, clears the legs, the path and the
// target and starts from the core's position and heading (the heading kept
// normalised twice). Shutdown (while initialised) ends the tow, takes the
// legs out of the watercraft manager's dock table, stops listening, lets go
// of the legs, the target and the effects, then runs the core's Shutdown.
//
// Oddities kept: the constructor's path loop writes each point only when the
// pointer is not null, but steps it on (a failed allocation would write at
// 0x10); the destructor body lets go of the references without clearing
// them and frees the byte vectors, not the generator; QueryInterface and
// Read answer AL only (ours zero-extends); Read reads its version word into
// the stream argument's own stack slot; the debug text does not test the
// watercraft manager for null; Init does not test the city or the effects
// manager for null, and normalises a zero heading into NaNs; the dock table
// is hashed by the leg's occupant pointer without testing the bucket count.
//
// Floats: Init's normalisation is x87 at 53 bits: (x*x + y*y) + z*z, its
// square root, 1 (0x00B0A18C) over it, each part times that (doubles here).
//
// Still the game's, at their addresses: the core's code (part vb), the
// drive parameters' reader 0x00567310 and the rest of the boat (part wc2),
// the ten adjustor thunks, the string, vector, generator and effect code,
// the message server and the city.

#include "core/automata_watercraft.h"

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

namespace opensc4::automata_watercraft {
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
    log::Write("automata watercraft: first %s", name);
}

constexpr std::size_t kAddRef = 0x04, kRelease = 0x08;
constexpr std::size_t kVtable4 = 0x04;
constexpr std::size_t kStreamUint16 = 0x1C; // cIGZOStream / cIGZIStream slot 7

void Release(void* object) { Virtual<std::uint32_t>(object, kRelease); }

// A referenced field cleared, then let go of (the game's order).
void Drop(char* self, std::size_t at)
{
    if (void* const object = At<void*>(self, at)) {
        At<void*>(self, at) = nullptr;
        Release(object);
    }
}

bool Initialized(char* self) { return Virtual<std::uint8_t>(self + kVtable4, slot::kInitialized) != 0; }

void Free(void* block) { CallCdecl<void>(addr::SC4_Free, block); }

// The six vtables the constructor sets (+0x00, +0x04, +0x08, +0x0C, +0x20,
// +0x38).
constexpr std::uintptr_t kTables[6] = {addr::cSC4Watercraft_Vtable,   addr::cSC4Watercraft_Vtable4,
                                       addr::cSC4Watercraft_Vtable8,  addr::cSC4Watercraft_VtableC,
                                       addr::cSC4Watercraft_Vtable20, addr::cSC4Watercraft_Vtable38};

// The float the constructor copies into the points.
std::uint32_t Unset() { return Global<std::uint32_t>(addr::cSC4Watercraft_DefaultFloat); }

// A cIGZString on the stack (vtable, begin, end, capacity, a word
// SC4String::FromText zeroes), destroyed as the game inlines it: the vtable
// stored again, the buffer freed.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

// The point at `at` divided by its length (x87 at 53 bits): 1 (0x00B0A18C)
// over the root of (x*x + y*y) + z*z, each part times that.
void Normalise(char* self, std::size_t at)
{
    const double x = At<float>(self, at), y = At<float>(self, at + 4), z = At<float>(self, at + 8);
    const double scale = static_cast<double>(Global<float>(addr::cSC4Watercraft_One)) / std::sqrt((x * x + y * y) + z * z);
    At<float>(self, at) = static_cast<float>(scale * At<float>(self, at));
    At<float>(self, at + 4) = static_cast<float>(scale * At<float>(self, at + 4));
    At<float>(self, at + 8) = static_cast<float>(scale * At<float>(self, at + 8));
}

// ====================================================================================
// The objects

// 0x0056D9A0, the constructor, plain ret: the core's with four zero words,
// the six vtables; the legs' references and byte vectors zero and their
// points 0x00B22A80's; +0x280 and +0x284 zero; the path 11 zeroed 16-byte
// points from SC4::stl_allocate (0xB0 bytes); the points +0x2E8..+0x314
// 0x00B22A80's; the target and its byte vector zero; the generator seeded
// with -1; the effects zero; the core's flag 0x10 at +0x33 and the type.
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Construct", logged);
    CallThis<char*>(addr::cSC4AutomatonCore_Construct, self, 0u, 0u, 0u, 0u);
    static constexpr std::size_t kParts[6] = {0x00, 0x04, 0x08, 0x0C, 0x20, 0x38};
    for (int i = 0; i < 6; ++i)
        At<std::uintptr_t>(self, kParts[i]) = kTables[i];
    for (const std::size_t leg : {field::kLegA, field::kLegB, field::kLeg}) {
        for (std::size_t i = 0; i < 0x10; i += 4)
            At<std::uint32_t>(self, leg + i) = 0;
        for (std::size_t i = 0; i < 0x18; i += 4)
            At<std::uint32_t>(self, leg + field::kLegPoints + i) = Unset();
    }
    At<std::uint32_t>(self, field::kTow) = 0;
    At<std::uint32_t>(self, field::kRef284) = 0;
    At<std::uint32_t>(self, field::kPath) = 0;
    At<std::uint32_t>(self, field::kPath + 4) = 0;
    At<std::uint32_t>(self, field::kPath + 8) = 0;
    char* const points = CallCdecl<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(0xB0));
    At<char*>(self, field::kPath + 8) = points + 0xB0;
    At<char*>(self, field::kPath) = points;
    At<char*>(self, field::kPath + 4) = points;
    char* point = points;
    for (int i = 0; i < 11; ++i, point += 0x10)
        if (point)
            std::memset(point, 0, 0x10);
    At<char*>(self, field::kPath + 4) = point;
    for (std::size_t i = 0; i < 0x30; i += 4)
        At<std::uint32_t>(self, field::kStartPoint + i) = Unset();
    for (std::size_t i = 0; i < 0x10; i += 4)
        At<std::uint32_t>(self, field::kTarget + i) = 0;
    {
        // determinism.fixed_seed keys a clock seed by its call site: the
        // game's return address.
        const fixed_seed::SiteOverride site(0x0056DC28);
        CallThis<void*>(addr::SC4Random_ConstructSeeded, self + field::kGenerator, 0xFFFFFFFFu);
    }
    for (std::size_t i = 0; i < 4 * field::kEffectCount; i += 4)
        At<std::uint32_t>(self, field::kEffects + i) = 0;
    At<std::uint8_t>(self, field::kFlags33) |= 0x10;
    At<std::uint32_t>(self, field::kType28) = kType;
    return self;
}

// 0x00514820, the GZCOM factory (cdecl): 0xDC8 bytes from SC4::operator_new
// and the constructor (a tail jump); null when the block is.
void* __cdecl Create()
{
    static bool logged;
    FirstCall("Create", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, kSize);
    if (!block)
        return nullptr;
    return CallThis<char*>(addr::cSC4Watercraft_Construct, block);
}

// 0x0056DD20, the destructor body, plain ret: the effects from the last,
// the target's byte vector and the target, the path, +0x284, the tow and
// each leg's byte vector and occupant let go of (not cleared), then the
// core's (a tail jump).
void* __fastcall Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Destroy", logged);
    for (std::size_t i = field::kEffectCount; i-- > 0;)
        if (void* const effect = At<void*>(self, field::kEffects + 4 * i))
            Release(effect);
    if (void* const bytes = At<void*>(self, field::kTargetPath))
        Free(bytes);
    if (void* const target = At<void*>(self, field::kTarget))
        Release(target);
    if (void* const path = At<void*>(self, field::kPath))
        Free(path);
    if (void* const object = At<void*>(self, field::kRef284))
        Release(object);
    if (void* const tow = At<void*>(self, field::kTow))
        Release(tow);
    for (const std::size_t leg : {field::kLeg, field::kLegB, field::kLegA}) {
        if (void* const bytes = At<void*>(self, leg + field::kLegBytes))
            Free(bytes);
        if (void* const occupant = At<void*>(self, leg + field::kLegRef))
            Release(occupant);
    }
    return CallThis<void*>(addr::cSC4AutomatonCore_Destroy, self);
}

// 0x0056DD00, the deleting destructor (flags), ret 4 (the +0xC part's slot
// 22 through its thunk): the body, then with flag 1 SC4::operator_delete
// (no null test).
char* __fastcall DeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    CallThis<void*>(addr::cSC4Watercraft_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x00566F30, slot 0 (iid, out), ret 8: its class ID answers the boat
// (AddRef, AL 1); the rest the core's (a tail jump).
std::uint32_t __fastcall QueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    if (iid != kClassId)
        return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_QueryInterface, self, iid, out);
    *out = self;
    Virtual<std::uint32_t>(self, kAddRef);
    return 1;
}

// 0x0056DC60, the serializable part's slot 5: its class ID.
std::uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// The save (this = the boat + 8, the serializable part)

// 0x00566EE0, +8 slot 3 (stream), ret 4: the version word 2, then the
// core's Write (its EAX).
std::uint32_t __fastcall Write(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("Write", logged);
    Virtual<std::uint8_t>(s, kStreamUint16, static_cast<std::uint32_t>(kSaveVersion));
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Write, part, s);
}

// 0x00566F00, +8 slot 4 (stream), ret 4: the version word read into the
// stream argument's own stack slot; AL 0 unless it is 2, else the core's
// Read (its EAX).
std::uint32_t __fastcall Read(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("Read", logged);
    std::uint16_t version = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(s));
    Virtual<std::uint8_t>(s, kStreamUint16, &version);
    if (version != kSaveVersion)
        return 0;
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Read, part, s);
}

// ====================================================================================
// The life cycle

// The watercraft manager's dock table (+0xA8C; buckets +0xA90..+0xA94) at
// the leg occupant `key`, hashed by its address (the key held for the
// while); the node's boat at `slotAt` cleared and let go of when it is
// `boat`.
void Unlist(char* manager, void* key, void* boat, std::size_t slotAt)
{
    if (key)
        Virtual<std::uint32_t>(key, kAddRef);
    char** const buckets = At<char**>(manager, manager::kBuckets);
    const std::uint32_t count =
        static_cast<std::uint32_t>(At<char**>(manager, manager::kBucketsEnd) - buckets);
    char* node = buckets[reinterpret_cast<std::uintptr_t>(key) % count];
    while (node && At<void*>(node, manager::kNodeKey) != key)
        node = At<char*>(node, 0);
    if (key)
        Release(key);
    if (node && At<void*>(node, slotAt) == boat) {
        if (void* const listed = At<void*>(node, slotAt)) {
            At<void*>(node, slotAt) = nullptr;
            Release(listed);
        }
    }
}

// 0x0056CFE0, on the watercraft manager (leg occupant, boat), ret 8: the
// node's first boat (+8).
void __fastcall UnlistFirst(char* manager, void*, void* key, void* boat)
{
    static bool logged;
    FirstCall("UnlistFirst", logged);
    Unlist(manager, key, boat, manager::kNodeFirst);
}

// 0x0056D060, the same for the node's second boat (+0xC).
void __fastcall UnlistSecond(char* manager, void*, void* key, void* boat)
{
    static bool logged;
    FirstCall("UnlistSecond", logged);
    Unlist(manager, key, boat, manager::kNodeSecond);
}

// 0x0056DE40, slot 3: AL 1 at once when the occupant part is initialised.
// Else the core's Init and the drive parameters' reader (0x00567310), the
// tow and +0x284 let go of; the six effects by name from the city's effects
// manager (slot 85; each field let go of first, then its slot 7 (name,
// field)); the automata flags 0x8000, 0x2000, 0x20000 and 4 set; the
// message 0xAC400A31 listened to (when there is a server); the byte +0x2D8,
// the legs (their bytes +0x31 and occupants), the leg under way, the path
// (emptied by SC4Vector16::Copy), the bytes, words and target zero; the
// start and the point from the core's position, the start heading and the
// heading from its heading, both normalised; +0x2CA 1, +0x2BC 0; slot 77
// (1). AL 1.
std::uint32_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("Init", logged);
    if (Initialized(self))
        return 1;
    CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Init, self);
    CallThis<void>(addr::cSC4Watercraft_ReadDriveParams, self);
    Drop(self, field::kTow);
    Drop(self, field::kRef284);
    void* const effects = Virtual<void*>(Global<void*>(addr::SC4_City), slot::kCityEffects);
    static const char* const kNames[field::kEffectCount] = {"SMLmushroomcloud", "boat_grounding",    "boat_splash",
                                                            "tugropethrow",     "tugropeconnection", "waterski"};
    GameString names[field::kEffectCount];
    for (std::size_t i = 0; i < field::kEffectCount; ++i)
        CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&names[i]), kNames[i]);
    for (std::size_t i = 0; i < field::kEffectCount; ++i) {
        const std::size_t at = field::kEffects + 4 * i;
        Drop(self, at);
        const char* const name = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&names[i]));
        Virtual<std::uint8_t>(effects, slot::kMakeEffect, name, &At<void*>(self, at));
    }
    for (const std::uint32_t flag : {0x8000u, 0x2000u, 0x20000u, 4u})
        Virtual<void>(self, slot::kSetFlag, flag, 1u);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<std::uint8_t>(server, slot::kRegister, self + field::kMessages, slot::kMessage);
    At<std::uint8_t>(self, field::kByte2D8) = 0;
    At<std::uint8_t>(self, field::kLegA + field::kLegListed) = 0;
    Drop(self, field::kLegA);
    At<std::uint8_t>(self, field::kLegB + field::kLegListed) = 0;
    Drop(self, field::kLegB);
    At<std::uint8_t>(self, field::kLeg + field::kLegListed) = 0;
    Drop(self, field::kLeg);
    {
        char tag = 0; // the game passes the address of a slot of its stack
        char* const end = At<char*>(self, field::kPath + 4);
        At<char*>(self, field::kPath + 4) = CallCdecl<char*>(addr::SC4Vector16_Copy, end, end,
                                                             At<char*>(self, field::kPath),
                                                             static_cast<void*>(&tag), 0u);
    }
    At<std::uint32_t>(self, field::kWordDAC) = 0;
    for (const std::size_t at : {field::kByte288, field::kByte289, field::kByte2B0, field::kByte2B1, field::kByte2B2,
                                 field::kByte2CB, field::kByte2CC, field::kByte340})
        At<std::uint8_t>(self, at) = 0;
    Drop(self, field::kTarget);
    for (const std::size_t at : {field::kWord2B4, field::kWord2B8, field::kWord2D0, field::kWord2D4, field::kSpeed,
                                 field::kWord2E4, field::kTurn, field::kWord2C0, field::kWord2C4})
        At<std::uint32_t>(self, at) = 0;
    for (std::size_t i = 0; i < 12; i += 4) {
        At<std::uint32_t>(self, field::kStartHeading + i) = At<std::uint32_t>(self, field::kHeading58 + i);
        At<std::uint32_t>(self, field::kHeading + i) = At<std::uint32_t>(self, field::kHeading58 + i);
    }
    for (std::size_t i = 0; i < 12; i += 4) {
        At<std::uint32_t>(self, field::kStartPoint + i) = At<std::uint32_t>(self, field::kPosition + i);
        At<std::uint32_t>(self, field::kPoint + i) = At<std::uint32_t>(self, field::kPosition + i);
    }
    At<std::uint8_t>(self, field::kByte2CA) = 1;
    Normalise(self, field::kStartHeading);
    Normalise(self, field::kHeading);
    At<std::uint32_t>(self, field::kWord2BC) = 0;
    Virtual<void>(self, slot::kSlot77, 1u);
    for (std::size_t i = field::kEffectCount; i-- > 0;)
        DestroyString(names[i]);
    return 1;
}

// 0x0056D0E0, slot 4: while the occupant part is initialised, the tow
// given state 0x49 (slot 52: 0x49, 0, -1.0, 0, 0, 0, 0) and let go of,
// +0x284 let go of; with a watercraft manager each listed leg (byte +0x31)
// out of its dock table (0x0056D060, then 0x0056CFE0); the message
// 0xAC400A31 no longer listened to (when there is a server); the legs
// (their bytes and occupants), the leg under way and the target let go of;
// each effect stopped (slot 4 (1)) and let go of. Then the core's Shutdown
// (a tail jump; its EAX).
std::uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("Shutdown", logged);
    if (Initialized(self)) {
        if (void* const tow = At<void*>(self, field::kTow)) {
            Virtual<void>(tow, slot::kPushState, 0x49u, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
            Drop(self, field::kTow);
        }
        Drop(self, field::kRef284);
        if (char* const manager = Virtual<char*>(Global<void*>(addr::SC4_City), slot::kCityWatercraft))
            for (const std::size_t leg : {field::kLegA, field::kLegB})
                if (At<std::uint8_t>(self, leg + field::kLegListed)) {
                    CallThis<void>(addr::cSC4WatercraftManager_UnlistSecond, manager, At<void*>(self, leg),
                                   static_cast<void*>(self));
                    CallThis<void>(addr::cSC4WatercraftManager_UnlistFirst, manager, At<void*>(self, leg),
                                   static_cast<void*>(self));
                }
        if (void* const server = Global<void*>(addr::SC4_MessageServer))
            Virtual<std::uint8_t>(server, slot::kUnregister, self + field::kMessages, slot::kMessage);
        At<std::uint8_t>(self, field::kLegA + field::kLegListed) = 0;
        Drop(self, field::kLegA);
        At<std::uint8_t>(self, field::kLegB + field::kLegListed) = 0;
        Drop(self, field::kLegB);
        At<std::uint8_t>(self, field::kLeg + field::kLegListed) = 0;
        Drop(self, field::kLeg);
        Drop(self, field::kTarget);
        for (std::size_t i = 0; i < field::kEffectCount; ++i) {
            const std::size_t at = field::kEffects + 4 * i;
            if (void* const effect = At<void*>(self, at)) {
                Virtual<void>(effect, slot::kEffectStop, 1u);
                Drop(self, at);
            }
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_Shutdown, self);
}

// ====================================================================================
// The queries

// 0x00567D80, slot 16 (point, heading), ret 8: each given one copied into
// the point (+0x2F4) and the heading (+0x30C), then the core's
// SetPlacement with both, slots 104 and 107.
void __fastcall SetPlacement(char* self, void*, const float* point, const float* heading)
{
    static bool logged;
    FirstCall("SetPlacement", logged);
    if (point)
        std::memcpy(self + field::kPoint, point, 12);
    if (heading)
        std::memcpy(self + field::kHeading, heading, 12);
    CallThis<void>(addr::cSC4AutomatonCore_SetPlacement, self, point, heading);
    Virtual<void>(self, slot::kSlot104);
    Virtual<void>(self, slot::kSlot107);
}

// 0x00570AE0, slot 93: the debug text in a static string (0x00B22AA4, made
// once under the guard bit 1 of 0x00B22AB8: an empty 8-byte buffer, its
// destructor 0x00A7B5D0 given to atexit): the tow (0 or 1), the watercraft
// manager's +0xA9C and +0xAC4, the bytes +0x289 and +0x288, the turn and
// the speed, the point's cell (the city's slot 95, -1 each first), the
// point and the heading. EAX the string.
void* __fastcall DebugText(char* self, void*)
{
    static bool logged;
    FirstCall("DebugText", logged);
    auto& guard = *reinterpret_cast<std::uint32_t*>(addr::cSC4Watercraft_DebugStringGuard);
    char* const text = reinterpret_cast<char*>(addr::cSC4Watercraft_DebugString);
    if (!(guard & 1)) {
        guard |= 1;
        At<std::uint32_t>(text, 4) = 0;
        At<std::uint32_t>(text, 8) = 0;
        At<std::uint32_t>(text, 0xC) = 0;
        char* const buffer = CallCdecl<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(8));
        At<char*>(text, 4) = buffer;
        At<char*>(text, 8) = buffer;
        At<char*>(text, 0xC) = buffer + 8;
        buffer[0] = 0;
        At<std::uintptr_t>(text, 0) = addr::SC4String_Vtable;
        At<std::uint32_t>(text, 0x10) = 0;
        CallCdecl<int>(addr::SC4_atexit, addr::cSC4Watercraft_DebugStringDestroy);
    }
    void* const city = Global<void*>(addr::SC4_City);
    char* const manager = Virtual<char*>(city, slot::kCityWatercraft);
    std::int32_t cellX = -1, cellZ = -1;
    Virtual<void>(Global<void*>(addr::SC4_City), slot::kCellOf, At<float>(self, field::kPoint),
                  At<float>(self, field::kPoint + 8), &cellX, &cellZ);
    CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(text),
                    reinterpret_cast<const char*>(addr::cSC4Watercraft_DebugFormat),
                    At<void*>(self, field::kTow) ? 1u : 0u, At<std::uint32_t>(manager, manager::kCountA9C),
                    At<std::uint32_t>(manager, manager::kCountAC4),
                    static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kByte289)),
                    static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kByte288)),
                    static_cast<double>(At<float>(self, field::kTurn)),
                    static_cast<double>(At<float>(self, field::kSpeed)), cellX, cellZ,
                    static_cast<double>(At<float>(self, field::kPoint)),
                    static_cast<double>(At<float>(self, field::kPoint + 4)),
                    static_cast<double>(At<float>(self, field::kPoint + 8)),
                    static_cast<double>(At<float>(self, field::kHeading)),
                    static_cast<double>(At<float>(self, field::kHeading + 4)),
                    static_cast<double>(At<float>(self, field::kHeading + 8)));
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
            log::Write("automata watercraft %s: no address entry for 0x%08X", what,
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
    log::Write("automata watercraft %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Part wc1.
const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4Watercraft_Create, Create),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4Watercraft_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4Watercraft_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4Watercraft_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Write, Write),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Read, Read),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Init, Init),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_UnlistFirst, UnlistFirst),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_UnlistSecond, UnlistSecond),
    OPENSC4_DETOUR(addr::cSC4Watercraft_SetPlacement, SetPlacement),
    OPENSC4_DETOUR(addr::cSC4Watercraft_DebugText, DebugText),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.watercraft", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Watercraft_Construct,
                        "Our boat (cSC4Watercraft), the objects: the factory, the constructor, the destructor body "
                        "and the deleting destructor, QueryInterface and GetGZCLSID; Write and Read; Init and "
                        "Shutdown with the watercraft manager's two dock-table helpers; slot 16 (the placement) and "
                        "slot 93 (the debug text) (14 detours, part wc1; src/core/automata_watercraft.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_watercraft
