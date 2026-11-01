// Our boat, parts wc1, wc2a and wc2b (build 641): cSC4Watercraft on the automata core
// (src/core/automata_vehicle.cpp has the core's ported slots; the core's own
// code stays the game's). wc1: the objects (the factory, the constructor,
// the destructor body and the deleting destructor, QueryInterface and
// GetGZCLSID), the save (Write and Read), the life cycle (Init and Shutdown,
// with the two dock-table helpers Shutdown calls) and the queries (slot 16,
// the placement, and slot 93, the debug text). wc2a: the drive parameters'
// reader, the legs' copy and assignment, beginning a leg, picking and
// beginning the next leg of a ferry route, the sign of a float and the tow's
// release. wc2b: slots 110 and 111 (entering and leaving a state), the
// leg under way unlisted, the dock table's two listings, the checks (may
// drive, outside the city, grounded), a path point erased and the next leg
// reached. The layout is in
// src/core/automata_watercraft.h and the survey in docs/automata.md, "Part
// wc". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.watercraft: the 14 detours in kObject (wc1), the 8
//   in kDriveList (wc2a) and the 10 in kStateList (wc2b) below.
//
// What it does. The boat is 0xDC8 bytes from SC4::operator_new and goes
// back through SC4::operator_delete. Its constructor runs the core's with
// four zero words, sets its six vtables, its references, byte vectors and points, a path of
// 11 zeroed points, its own generator seeded with -1, the type 0x897F7EC2
// and the core's flag 0x10 at +0x33. Its save is the core's after a version
// word of 2. Init (once, while the occupant part is not initialised) runs
// the core's Init and the drive parameters' reader (0x00567310), makes its
// six effects through the city's effects manager, sets four automata flags,
// listens to the message 0xAC400A31, clears the legs, the path and the
// target and starts from the core's position and heading (the start heading
// and the heading both normalised). Shutdown (while initialised) ends the tow, takes the
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
// wc2a's oddities: the drive parameters take a zero of either sign for unset
// and keep a NaN (made quiet, as x87 loads it); the next leg is the second
// only when the first point matches the first leg's (a NaN picks the first);
// the tow's release reads the tow again after its slot 53.
//
// wc2b's oddities: slot 110 answers the core's AL (ours zero-extends);
// slot 111's 0x4D reads +0x284 again after releasing its tow; the next leg
// reached uses its occupant after letting go of it; the grounded check
// leaves the cell outputs unset and the outside check its four cells (ours
// zero); the dock table's listing holds the key for the lookup only.
//
// Floats: Init's normalisation is x87 at 53 bits: (x*x + y*y) + z*z, its
// square root, 1 (0x00B0A18C) over it, each part times that (doubles here).
//
// Still the game's, at their addresses: the core's code (part vb), the
// rest of the boat (part wc2: slots 72, 109, 115, 121, the +0x38 part's
// slot 3 and their helpers), the group's drive parameters 0x004FC8E0,
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
// The drive parameters (part wc2a)

// A float as the game's x87 code loads and stores it: a signalling NaN comes
// back quiet.
std::uint32_t Loaded(std::uint32_t bits)
{
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) && !(bits & 0x00400000u))
        bits |= 0x00400000u;
    return bits;
}

float AsFloat(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

std::uint32_t BitsOf(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// x87's quotient of two floats rounded to single (at 53 bits the same as a
// single division), except that of two NaNs x87 answers the one with the
// larger significand (SSE the dividend).
std::uint32_t Quotient(std::uint32_t dividend, std::uint32_t divisor)
{
    const auto isNaN = [](std::uint32_t v) { return (v & 0x7F800000u) == 0x7F800000u && (v & 0x007FFFFFu); };
    if (isNaN(dividend) && isNaN(divisor))
        return Loaded((divisor & 0x007FFFFFu) > (dividend & 0x007FFFFFu) ? divisor : dividend);
    return BitsOf(AsFloat(dividend) / AsFloat(divisor));
}

// How a parameter's zero is replaced: by its default, or (three of them) by
// one already read: the negated +0x368, +0x38C over +0x360, +0x394 over
// +0x368.
enum class Zero : std::uint8_t { Default, Negated368, Ratio388, Ratio390 };

// The 40 parameters in the game's order: the offset in the template's block,
// the boat's field, the value without a template (each default's constant in
// the game: 0x00A8C950, 0x00A867A4, 0x00A81228, 0x00A880D8, 0x00A94D54,
// 0x00A84D28, 0x00A94D60, 0x00A97D74, 0x00A8825C, -, 0x00A867A4, 0x00A84D24,
// 0x00A84D28, 0x00A867A4, 0x00A867A4, 0x00A8C950, 0x00A84D2C, 0x00A9DA80, -,
// 0x00A949F0, -, 0x00A9DA7C, 0x00A9DA78, 0x00A9BEF8, 0x00A9DA74, 0x00A9DA70,
// 0x00A94D50, 0x00A84D2C, 0x00A8821C, 0x00A98F34, 0x00A9DA6C, 0x00A9DA68,
// 0x00A949F4, 0x00A81054, 0x00A8825C, 0x00A80AA0, 0x00A9DA64, 0x00A873A4,
// 0x00A81228, 0x00A81228; the three derived ones written as constants there).
struct DriveParam {
    std::uint16_t from, to;
    std::uint32_t fallback;
    Zero zero;
};
constexpr DriveParam kDrive[] = {
    {0x00, 0x344, 0x3DCCCCCD, Zero::Default},  {0x04, 0x348, 0x3A83126F, Zero::Default},
    {0x08, 0x34C, 0x3F800000, Zero::Default},  {0x0C, 0x350, 0x3D4CCCCD, Zero::Default},
    {0x10, 0x354, 0x3E4CCCCD, Zero::Default},  {0x14, 0x358, 0x40A00000, Zero::Default},
    {0x18, 0x35C, 0xBDCCCCCD, Zero::Default},  {0x1C, 0x360, 0x3E99999A, Zero::Default},
    {0x24, 0x368, 0x40000000, Zero::Default},  {0x20, 0x364, 0xC0000000, Zero::Negated368},
    {0x28, 0x36C, 0x3A83126F, Zero::Default},  {0x2C, 0x370, 0x42480000, Zero::Default},
    {0x30, 0x374, 0x40A00000, Zero::Default},  {0x34, 0x378, 0x3A83126F, Zero::Default},
    {0x38, 0x37C, 0x3A83126F, Zero::Default},  {0x3C, 0x380, 0x3DCCCCCD, Zero::Default},
    {0x40, 0x384, 0x3F000000, Zero::Default},  {0x48, 0x38C, 0x3EA0D97C, Zero::Default},
    {0x44, 0x388, 0x3F860A92, Zero::Ratio388}, {0x50, 0x394, 0x3E860A92, Zero::Default},
    {0x4C, 0x390, 0x3E060A92, Zero::Ratio390}, {0x54, 0x398, 0x45FA0000, Zero::Default},
    {0x58, 0x39C, 0x3F490FD0, Zero::Default},  {0x5C, 0x3A0, 0x3F733333, Zero::Default},
    {0x60, 0x3A4, 0x400BA058, Zero::Default},  {0x64, 0x3A8, 0x3F860A92, Zero::Default},
    {0x68, 0x3AC, 0x41200000, Zero::Default},  {0x6C, 0x3B0, 0x3F000000, Zero::Default},
    {0x70, 0x3B4, 0x3E800000, Zero::Default},  {0x74, 0x3B8, 0x3F400000, Zero::Default},
    {0x78, 0x3BC, 0xC4FA0000, Zero::Default},  {0x7C, 0x3C0, 0x453B8000, Zero::Default},
    {0x80, 0x3C4, 0x43160000, Zero::Default},  {0x84, 0x3C8, 0x00000000, Zero::Default},
    {0x88, 0x3CC, 0x40000000, Zero::Default},  {0x8C, 0x3D0, 0x42C80000, Zero::Default},
    {0x90, 0x3D4, 0x43960000, Zero::Default},  {0x94, 0x3D8, 0x42B40000, Zero::Default},
    {0x98, 0x3DC, 0x3F800000, Zero::Default},  {0x9C, 0x3E0, 0x3F800000, Zero::Default},
};
static_assert(sizeof kDrive / sizeof kDrive[0] == field::kDriveParamsSize / 4, "40 drive parameters");

// 0x00567310, plain ret (Init's and the +0x38 part's slot 3's): the
// template's (slot 67) WatercraftDriveParams, its own (+0x60) or else its
// parent's up the chain (0x004FC8E0 on +0xC). Each float into +0x344..
// unless it is zero (either sign; a NaN is kept, made quiet as x87 loads
// it); a zero gives its default, or for +0x364 the negated +0x368, for
// +0x388 +0x38C over +0x360 and for +0x390 +0x394 over +0x368 (x87 at 53
// bits, rounded to single). Without a template or parameters every field its constant.
void __fastcall ReadDriveParams(char* self, void*)
{
    static bool logged;
    FirstCall("ReadDriveParams", logged);
    char* from = nullptr;
    if (char* const group = Virtual<char*>(self, slot::kTemplate)) {
        from = At<char*>(group, group::kDriveParams);
        if (!from)
            if (void* const parent = At<void*>(group, group::kParent))
                from = CallThis<char*>(addr::cSC4AutomataManager_GroupDriveParams, parent);
    }
    for (const DriveParam& p : kDrive) {
        std::uint32_t value = p.fallback;
        if (from) {
            const std::uint32_t given = At<std::uint32_t>(from, p.from);
            if (AsFloat(given) != 0.0f) {
                value = Loaded(given);
            } else {
                switch (p.zero) {
                case Zero::Default: break;
                case Zero::Negated368: value = At<std::uint32_t>(self, 0x368) ^ 0x80000000u; break;
                case Zero::Ratio388: value = Quotient(At<std::uint32_t>(self, 0x38C), At<std::uint32_t>(self, 0x360)); break;
                case Zero::Ratio390: value = Quotient(At<std::uint32_t>(self, 0x394), At<std::uint32_t>(self, 0x368)); break;
                }
            }
        }
        At<std::uint32_t>(self, p.to) = value;
    }
}

// ====================================================================================
// The legs (part wc2a)

// A leg by value (0x34 bytes, see the header).
struct Leg {
    std::uint32_t words[13];
};
static_assert(sizeof(Leg) == field::kLegSize, "a leg is 0x34 bytes");

// A leg's +0x10..+0x27 (the points), the byte +0x28, the word +0x2C and the
// bytes +0x30 and +0x31 copied (the bytes between left).
void CopyLegTail(char* to, const char* from)
{
    for (std::size_t i = field::kLegPoints; i < field::kLegByte28; i += 4)
        At<std::uint32_t>(to, i) = *reinterpret_cast<const std::uint32_t*>(from + i);
    to[field::kLegByte28] = from[field::kLegByte28];
    At<std::uint32_t>(to, field::kLegCount) = *reinterpret_cast<const std::uint32_t*>(from + field::kLegCount);
    to[field::kLegByte30] = from[field::kLegByte30];
    to[field::kLegListed] = from[field::kLegListed];
}

// 0x0056D8B0, a leg copy-constructed from `other`, ret 4: the occupant
// (AddRef), the byte vector (SC4ByteVector::CopyConstruct), the rest. EAX
// this.
char* __fastcall CopyLeg(char* self, void*, const char* other)
{
    static bool logged;
    FirstCall("CopyLeg", logged);
    void* const occupant = *reinterpret_cast<void* const*>(other);
    At<void*>(self, field::kLegRef) = occupant;
    if (occupant)
        Virtual<std::uint32_t>(occupant, kAddRef);
    CallThis<void*>(addr::SC4ByteVector_CopyConstruct, self + field::kLegBytes,
                    static_cast<const void*>(other + field::kLegBytes));
    CopyLegTail(self, other);
    return self;
}

// 0x0056D920, a leg assigned from `other`, ret 4: unless it is itself, a
// different occupant held (AddRef), stored, the old one let go of; then
// always the byte vector (SC4ByteVector::Assign) and the rest. EAX this.
char* __fastcall AssignLeg(char* self, void*, const char* other)
{
    static bool logged;
    FirstCall("AssignLeg", logged);
    if (other != self) {
        void* const given = *reinterpret_cast<void* const*>(other);
        void* const held = At<void*>(self, field::kLegRef);
        if (given != held) {
            if (given)
                Virtual<std::uint32_t>(given, kAddRef);
            At<void*>(self, field::kLegRef) = given;
            if (held)
                Release(held);
        }
    }
    CallThis<void*>(addr::SC4ByteVector_Assign, self + field::kLegBytes,
                    static_cast<const void*>(other + field::kLegBytes));
    CopyLegTail(self, other);
    return self;
}

// 0x0056F3F0, the boat begins a leg (by value), ret 0x34: the leg under way
// assigned from it (0x0056D920), its byte +0x28 1, its word +0x2C 2 when
// zero, its byte +0x30 0; the route iterator +0x28C started on its byte
// vector; the path emptied (SC4Vector16::Copy). The argument's byte vector
// freed and its occupant let go of.
void __fastcall BeginLeg(char* self, void*, Leg leg)
{
    static bool logged;
    FirstCall("BeginLeg", logged);
    char* const given = reinterpret_cast<char*>(&leg);
    CallThis<char*>(addr::cSC4Watercraft_AssignLeg, self + field::kLeg, static_cast<const char*>(given));
    At<std::uint8_t>(self, field::kLeg + field::kLegByte28) = 1;
    if (!At<std::uint32_t>(self, field::kLeg + field::kLegCount))
        At<std::uint32_t>(self, field::kLeg + field::kLegCount) = 2;
    At<std::uint8_t>(self, field::kLeg + field::kLegByte30) = 0;
    CallThis<void>(addr::SC4RouteIterator_Init, self + field::kRoute,
                   static_cast<void*>(self + field::kLeg + field::kLegBytes));
    char* const end = At<char*>(self, field::kPath + 4);
    // The two arguments after the destination are not read; the game
    // passes the argument's address and 0.
    At<char*>(self, field::kPath + 4) = CallCdecl<char*>(addr::SC4Vector16_Copy, end, end,
                                                         At<char*>(self, field::kPath), static_cast<void*>(given), 0u);
    if (void* const bytes = At<void*>(given, field::kLegBytes))
        Free(bytes);
    if (void* const occupant = At<void*>(given, field::kLegRef))
        Release(occupant);
}

// 0x0056F480, the next leg of a ferry route into `out` (0x0056D8B0), ret
// 4: the second (+0x218) when the leg under way's first point is the first
// leg's (each part's difference within 0x00A9DA48, 1e-4; x87 at 53 bits),
// else the first (+0x1E4; a NaN too). EAX out.
char* __fastcall PickNextLeg(char* self, void*, char* out)
{
    static bool logged;
    FirstCall("PickNextLeg", logged);
    const double limit = static_cast<double>(AsFloat(0x38D1B717u));
    bool same = true;
    for (std::size_t i = 0; i < 12 && same; i += 4) {
        const double d = static_cast<double>(At<float>(self, field::kLeg + field::kLegPoints + i)) -
                         static_cast<double>(At<float>(self, field::kLegA + field::kLegPoints + i));
        same = std::fabs(d) < limit;
    }
    return CallThis<char*>(addr::cSC4Watercraft_CopyLeg, out,
                           static_cast<const char*>(self + (same ? field::kLegB : field::kLegA)));
}

// 0x0056F700, plain ret (slot 111's): with both legs listed (+0x215 and
// +0x249) the next leg (0x0056F480, built where the argument goes) begun
// (0x0056F3F0); else slot 13 (1).
void __fastcall NextLeg(char* self, void*)
{
    static bool logged;
    FirstCall("NextLeg", logged);
    if (At<std::uint8_t>(self, field::kLegA + field::kLegListed) &&
        At<std::uint8_t>(self, field::kLegB + field::kLegListed)) {
        // Ours builds it on the stack and hands it on bitwise (a leg holds no
        // pointer to itself).
        Leg next;
        CallThis<char*>(addr::cSC4Watercraft_PickNextLeg, self, reinterpret_cast<char*>(&next));
        CallThis<void>(addr::cSC4Watercraft_BeginLeg, self, next);
        return;
    }
    Virtual<void>(self, slot::kRemove, 1u);
}

// 0x00566E00, (x), ret 4 (this unread): -1 (0x00A8FE78) when x is below
// zero, else 1 (0x00A81228; a NaN too). ST0.
float __fastcall Sign(char*, void*, float x)
{
    static bool logged;
    FirstCall("Sign", logged);
    return x < 0.0f ? AsFloat(0xBF800000u) : AsFloat(0x3F800000u);
}

// 0x00567E80, plain ret: the tow's slot 53, then the tow (read again) and
// +0x284 cleared and let go of.
void __fastcall DropTow(char* self, void*)
{
    static bool logged;
    FirstCall("DropTow", logged);
    if (void* const tow = At<void*>(self, field::kTow))
        Virtual<void>(tow, slot::kSlot53);
    Drop(self, field::kTow);
    Drop(self, field::kRef284);
}

// ====================================================================================
// The states, the dock table and the checks (part wc2b)

constexpr std::uint32_t kFlagDocked = 0x10000000u, kFlag8000000 = 0x8000000u;

// The automaton's slot 52 (state, 0, -1.0, 0, 0, 0, 0).
void PushState(char* self, std::uint32_t type)
{
    Virtual<void>(self, slot::kPushState, type, 0u, 0xBF800000u, 0u, 0u, 0u, 0u);
}

// The watercraft manager's dock table at the leg occupant `key` (held for
// the while, see Unlist); when the node's boat at `slotAt` is empty, `boat`
// held and stored there, true.
bool List(char* manager, void* key, void* boat, std::size_t slotAt)
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
    if (!node || At<void*>(node, slotAt))
        return false;
    if (boat) {
        Virtual<std::uint32_t>(boat, kAddRef);
        At<void*>(node, slotAt) = boat;
    }
    return true;
}

// 0x0056CEC0, on the watercraft manager (leg occupant, boat), ret 8: into
// the node's first boat (+8) when empty. AL.
std::uint32_t __fastcall ListFirst(char* manager, void*, void* key, void* boat)
{
    static bool logged;
    FirstCall("ListFirst", logged);
    return List(manager, key, boat, manager::kNodeFirst) ? 1u : 0u;
}

// 0x0056CF50, the same for the node's second boat (+0xC).
std::uint32_t __fastcall ListSecond(char* manager, void*, void* key, void* boat)
{
    static bool logged;
    FirstCall("ListSecond", logged);
    return List(manager, key, boat, manager::kNodeSecond) ? 1u : 0u;
}

// 0x0056D4D0, plain ret: with a leg under way whose byte +0x28 is set,
// +0x2B0 0; with a watercraft manager the leg's occupant out of the dock
// table's first boat (0x0056CFE0), the byte 0 and the flag 0x10000000
// cleared.
void __fastcall UnlistLeg(char* self, void*)
{
    static bool logged;
    FirstCall("UnlistLeg", logged);
    if (!At<void*>(self, field::kLeg) || !At<std::uint8_t>(self, field::kLeg + field::kLegByte28))
        return;
    At<std::uint8_t>(self, field::kByte2B0) = 0;
    char* const manager = Virtual<char*>(Global<void*>(addr::SC4_City), slot::kCityWatercraft);
    if (!manager)
        return;
    CallThis<void>(addr::cSC4WatercraftManager_UnlistFirst, manager, At<void*>(self, field::kLeg),
                   static_cast<void*>(self));
    At<std::uint8_t>(self, field::kLeg + field::kLegByte28) = 0;
    Virtual<void>(self, slot::kSetFlag, kFlagDocked, 0u);
}

// 0x0056E340, slot 110 (state), ret 4: the core's EnterState first (its AL
// the answer; ours zero-extends), then by the state's type; see the address
// entry.
std::uint32_t __fastcall EnterState(char* self, void*, char* record)
{
    static bool logged;
    FirstCall("EnterState", logged);
    const std::uint8_t answer =
        static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4AutomatonCore_EnterState, self, record));
    if (!record)
        return answer;
    const auto setFlags = [self](bool both) {
        Virtual<void>(self, slot::kSetFlag, kFlagDocked, 1u);
        if (both)
            Virtual<void>(self, slot::kSetFlag, kFlag8000000, 1u);
    };
    switch (At<std::uint32_t>(record, state::kType)) {
    case 0x10:
        Virtual<void>(self, slot::kSlot77, 0u);
        At<std::uint32_t>(record, state::kWord30) = 0;
        At<std::uint32_t>(record, state::kWord234) = At<std::uint32_t>(self, field::kParam394);
        if (char* const group = Virtual<char*>(self, slot::kTemplate)) {
            if (char* const inherited = CallThis<char*>(addr::SC4AutomatonTemplate_FindInherited64, group))
                At<std::uint32_t>(record, state::kWord34) = At<std::uint32_t>(inherited, 8);
            else
                At<std::uint32_t>(record, state::kWord34) = 0x3F800000u;
        }
        At<std::uint32_t>(record, state::kWord144) = 0xF;
        At<std::uint32_t>(record, state::kWord178) = 0;
        At<std::uint32_t>(record, state::kWord1AC) = 0x44FA0000u;
        break;
    case 0x46:
    case 0x47:
        setFlags(true);
        Virtual<void>(self, slot::kSlot102, 0xBu, 0u);
        At<std::uint32_t>(self, field::kWord2C0) = 0;
        break;
    case 0x48:
    case 0x49:
        setFlags(true);
        Virtual<void>(self, slot::kRemove, At<std::uint32_t>(record, state::kType) == 0x48 ? 1u : 0u);
        break;
    case 0x4A:
        setFlags(false);
        At<std::uint8_t>(self, field::kByte288) = 1;
        At<std::uint8_t>(self, field::kByte2B0) = 1;
        CallThis<void>(addr::cSC4Watercraft_DropTow, self);
        return answer;
    case 0x4B:
        setFlags(false);
        At<std::uint8_t>(self, field::kByte289) = 1;
        At<std::uint32_t>(self, field::kSpeed) = 0;
        At<std::uint32_t>(self, field::kTurn) = 0;
        CallThis<void>(addr::cSC4Watercraft_DropTow, self);
        return answer;
    case 0x4C:
        At<std::uint32_t>(self, field::kSpeed) = 0;
        At<std::uint32_t>(self, field::kTurn) = 0;
        break;
    case 0x4D:
        At<std::uint8_t>(self, field::kByte2CB) = 1;
        setFlags(false);
        CallThis<void>(addr::cSC4Watercraft_UnlistLeg, self);
        return answer;
    default:
        return answer;
    }
    CallThis<void>(addr::cSC4Watercraft_DropTow, self);
    CallThis<void>(addr::cSC4Watercraft_UnlistLeg, self);
    return answer;
}

// 0x00570140, slot 111 (state), ret 4: by the state's type (see the address
// entry), then the core's LeaveState (its EAX).
std::uint32_t __fastcall LeaveState(char* self, void*, char* record)
{
    static bool logged;
    FirstCall("LeaveState", logged);
    if (record) {
        switch (At<std::uint32_t>(record, state::kType)) {
        case 0x10:
            Virtual<void>(self, slot::kSlot77, 1u);
            CallThis<void>(addr::cSC4Watercraft_DropTow, self);
            break;
        case 0x4A:
            At<std::uint8_t>(self, field::kByte288) = 0;
            CallThis<void>(addr::cSC4Watercraft_NextLeg, self);
            break;
        case 0x4B:
            Virtual<void>(self, slot::kSetFlag, kFlagDocked, 0u);
            At<std::uint8_t>(self, field::kByte289) = 0;
            break;
        case 0x4D:
            Virtual<void>(self, slot::kSetFlag, kFlagDocked, 0u);
            if (char* const other = At<char*>(self, field::kRef284)) {
                Drop(other, field::kTow);
                Drop(self, field::kRef284);
            }
            break;
        default:
            break;
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomatonCore_LeaveState, self, record);
}

// 0x005681B0, plain ret: the occupant part's slot 11 cells (x0, z0, x1, z1);
// true when the city's slot 99 denies a corner, asked in the order (x0, z0),
// (x0, z1), (x1, z0), (x1, z1). AL.
std::uint32_t __fastcall OutsideCity(char* self, void*)
{
    static bool logged;
    FirstCall("OutsideCity", logged);
    std::int32_t cells[4] = {};
    Virtual<void>(self + 4, slot::kCells, static_cast<void*>(cells));
    const std::int32_t xs[4] = {cells[0], cells[0], cells[2], cells[2]};
    const std::int32_t zs[4] = {cells[1], cells[3], cells[1], cells[3]};
    void* const city = Global<void*>(addr::SC4_City);
    for (int i = 0; i < 4; ++i)
        if (!Virtual<std::uint8_t>(city, slot::kInCity, xs[i], zs[i]))
            return 1;
    return 0;
}

// 0x005698E0, plain ret: false when slot 31 (1) answers; false with the
// state 0x48 pushed when a corner is outside the city (0x005681B0); false
// in the states 0x46 and 0x47 (slot 59, asked twice); else true. AL.
std::uint32_t __fastcall MayDrive(char* self, void*)
{
    static bool logged;
    FirstCall("MayDrive", logged);
    if (Virtual<std::uint8_t>(self, slot::kSlot31, 1u))
        return 0;
    if (CallThis<std::uint8_t>(addr::cSC4Watercraft_OutsideCity, self)) {
        PushState(self, 0x48);
        return 0;
    }
    if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x46)
        return 0;
    if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x47)
        return 0;
    return 1;
}

// 0x00567DF0, plain ret: the point's cell (city slot 95; the game leaves its
// outputs unset, ours zero); within the city (slot 99), true when the
// terrain's slot 29 (ST0 as it comes) is below its slot 18 at the cell
// (stored as a float). AL.
std::uint32_t __fastcall Grounded(char* self, void*)
{
    static bool logged;
    FirstCall("Grounded", logged);
    std::int32_t cellX = 0, cellZ = 0;
    void* const city = Global<void*>(addr::SC4_City);
    Virtual<void>(city, slot::kCellOf, At<float>(self, field::kPoint), At<float>(self, field::kPoint + 8), &cellX,
                  &cellZ);
    if (!Virtual<std::uint8_t>(Global<void*>(addr::SC4_City), slot::kInCity, cellX, cellZ))
        return 0;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    // Both read as doubles: ST0 whole (x87 at 53 bits).
    const float height = static_cast<float>(Virtual<double>(terrain, slot::kHeightAt, cellX, cellZ));
    const double water = Virtual<double>(terrain, slot::kWaterLevel);
    return water < static_cast<double>(height) ? 1u : 0u;
}

// 0x0056BD50, this = a vector of 16-byte points (begin, end), ret 4: the
// points after `at` moved down one (SC4Vector16::Copy; the game passes at's
// own argument slot and 0 after, unread), the end stepped back. EAX at.
char* __fastcall ErasePoint(char* vector, void*, char* at)
{
    static bool logged;
    FirstCall("ErasePoint", logged);
    char* const end = At<char*>(vector, 4);
    if (at + 0x10 != end)
        CallCdecl<char*>(addr::SC4Vector16_Copy, at + 0x10, end, at, static_cast<void*>(&at), 0u);
    At<char*>(vector, 4) -= 0x10;
    return at;
}

// 0x005704D0, plain ret: the next leg (0x0056F480) on the stack, its bytes
// freed and its occupant let go of (then used); with an occupant nearer
// (its slot 15; x87 at 53 bits: the root of (dz*dz + dy*dy) + dx*dx) than
// +0x3D4 and a watercraft manager: in the state 0x4B listed as the first
// boat (0x0056CEC0), then slot 53 and the state 0x4A; else listed first
// (0x4A), or second (0x0056CF50, 0x4B), or the state 0x49.
void __fastcall ReachLeg(char* self, void*)
{
    static bool logged;
    FirstCall("ReachLeg", logged);
    Leg next;
    char* const leg = reinterpret_cast<char*>(&next);
    CallThis<char*>(addr::cSC4Watercraft_PickNextLeg, self, leg);
    void* const occupant = At<void*>(leg, field::kLegRef);
    if (void* const bytes = At<void*>(leg, field::kLegBytes))
        Free(bytes);
    if (occupant)
        Release(occupant);
    if (!occupant)
        return;
    std::uint32_t point[3] = {Unset(), Unset(), Unset()};
    Virtual<void>(occupant, slot::kGetPosition, static_cast<void*>(point));
    const double dx = static_cast<double>(At<float>(self, field::kPosition)) - static_cast<double>(AsFloat(point[0]));
    const double dy =
        static_cast<double>(At<float>(self, field::kPosition + 4)) - static_cast<double>(AsFloat(point[1]));
    const double dz =
        static_cast<double>(At<float>(self, field::kPosition + 8)) - static_cast<double>(AsFloat(point[2]));
    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (!(distance < static_cast<double>(At<float>(self, field::kParam3D4))))
        return;
    char* const manager = Virtual<char*>(Global<void*>(addr::SC4_City), slot::kCityWatercraft);
    if (!manager)
        return;
    if (Virtual<std::uint32_t>(self, slot::kStateType) == 0x4B) {
        if (!CallThis<std::uint8_t>(addr::cSC4WatercraftManager_ListFirst, manager, occupant, static_cast<void*>(self)))
            return;
        Virtual<void>(self, slot::kSlot53);
        PushState(self, 0x4A);
        return;
    }
    if (CallThis<std::uint8_t>(addr::cSC4WatercraftManager_ListFirst, manager, occupant, static_cast<void*>(self)))
        PushState(self, 0x4A);
    else if (CallThis<std::uint8_t>(addr::cSC4WatercraftManager_ListSecond, manager, occupant,
                                    static_cast<void*>(self)))
        PushState(self, 0x4B);
    else
        PushState(self, 0x49);
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

// Part wc2a.
const Detour kDriveList[] = {
    OPENSC4_DETOUR(addr::cSC4Watercraft_ReadDriveParams, ReadDriveParams),
    OPENSC4_DETOUR(addr::cSC4Watercraft_CopyLeg, CopyLeg),
    OPENSC4_DETOUR(addr::cSC4Watercraft_AssignLeg, AssignLeg),
    OPENSC4_DETOUR(addr::cSC4Watercraft_BeginLeg, BeginLeg),
    OPENSC4_DETOUR(addr::cSC4Watercraft_PickNextLeg, PickNextLeg),
    OPENSC4_DETOUR(addr::cSC4Watercraft_NextLeg, NextLeg),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Sign, Sign),
    OPENSC4_DETOUR(addr::cSC4Watercraft_DropTow, DropTow),
};

// Part wc2b.
const Detour kStateList[] = {
    OPENSC4_DETOUR(addr::cSC4Watercraft_EnterState, EnterState),
    OPENSC4_DETOUR(addr::cSC4Watercraft_LeaveState, LeaveState),
    OPENSC4_DETOUR(addr::cSC4Watercraft_UnlistLeg, UnlistLeg),
    OPENSC4_DETOUR(addr::cSC4Watercraft_MayDrive, MayDrive),
    OPENSC4_DETOUR(addr::cSC4Watercraft_OutsideCity, OutsideCity),
    OPENSC4_DETOUR(addr::cSC4Watercraft_Grounded, Grounded),
    OPENSC4_DETOUR(addr::cSC4Watercraft_ErasePoint, ErasePoint),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ListFirst, ListFirst),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ListSecond, ListSecond),
    OPENSC4_DETOUR(addr::cSC4Watercraft_ReachLeg, ReachLeg),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kDriveList, sizeof kDriveList / sizeof kDriveList[0], "drive") &&
           InstallList(kStateList, sizeof kStateList / sizeof kStateList[0], "state");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.watercraft", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4Watercraft_Construct,
                        "Our boat (cSC4Watercraft), the objects: the factory, the constructor, the destructor body "
                        "and the deleting destructor, QueryInterface and GetGZCLSID; Write and Read; Init and "
                        "Shutdown with the watercraft manager's two dock-table helpers; slot 16 (the placement) and "
                        "slot 93 (the debug text) (14 detours, part wc1); the drive parameters' reader, the four leg "
                        "helpers and the next leg, the sign and the tow's release (8, part wc2a); slots 110 and 111, the "
                        "leg unlisted, the dock table's two listings, three checks, a path point erased and the next "
                        "leg reached (10, part wc2b; src/core/automata_watercraft.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_watercraft
