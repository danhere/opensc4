// Our city situation manager, part c1 (build 641): the object (the factory,
// the constructor, the destructor body, the deleting destructor (slot 34),
// QueryInterface, AddRef, Release and the serializable part's GetGZCLSID),
// the life cycle (Init with the mission success map's operator[] and insert,
// Shutdown with the release of the controlled objects, the tracked automata
// and the list, and the list's clear), the queries and setters of slots 5
// to 18 and 26 to 33, and the save (Write, Read and the success map's
// insert). The class map is in src/core/automata_situations.h and the survey
// in docs/automata.md, "Part c". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Groups, off by default:
//   simulators.automata.situations.object (8): kObject below.
//   simulators.automata.situations.life (8): kLife.
//   simulators.automata.situations.queries (22): kQueries.
//   simulators.automata.situations.save (3): kSave.
//
// What it does. The manager is the city's (city slot 49). Init, once:
//   - with the app's cheats switch (its slot 0x54) on, it asks the cheat
//     codes for notifications and registers the cheat `csitrigger`
//     (0x8C4136DB);
//   - it registers its message target (+4) with the message server for ten
//     messages and with the service 0x86AD10EC;
//   - it keeps the overlay manager (the dispatch service's slot 22), four
//     services, the lot manager, the city's slot 71 and the script context;
//   - it registers the Lua table `sitmgr` with seven functions and two
//     variables (the Lua callbacks are the game's, a later part);
//   - it runs HandleTuningParametersChanged, shows or hides the CSI by +0x9F0
//     and marks each tutorial the preferences list as done (+0x1334, zero
//     ended) with a success count of 1.
// Shutdown undoes it: the references dropped, the controlled objects
// (+0xAC4) told to stop (their slot 4 with 1) and released, the services
// released and the vector emptied, the registrations taken back, the active
// situation ended (slot 38, a later part), the tracked automata (+0xA08)
// and the list (+0xA04) released.
// The save is a version (2), the success map (count, then ID and uint16 per
// entry, in bucket order) and the CSI's visibility. Read takes versions up to
// 2 (the visibility from version 2 on) and sets +0x9F1.
//
// Oddities kept: QueryInterface's null branches write a null and then call
// AddRef through the object (the address is held in a volatile local, as in
// parts a1 and s1), and an unknown ID leaves *out alone; Release at a count
// of 1 (or 0) deletes without counting down; Init calls the app's slot 0x54
// without a null test at its start, but tests the app at its end; Read reads
// the version into its own argument's slot; Write pushes each value's word
// with the high half of the part's address above it (the stream reads the
// word); a failed read in Read's loop keeps the previous entry's key or
// value; GetMissionSuccessCount answers the count in AX with the high half
// of the bucket vector's address above it; the destructor body leaves the
// cRZDate and the SC4Random alone; the list clear does not zero what it
// releases.
//
// Floats: GetCurrentTimeRemainingSeconds subtracts the situation's time from
// the time limit (slot 13's ST0) on the x87 stack; ours answers a double in
// ST0 so that the difference is not rounded to a float. CountdownIsInProgress
// is true for a countdown above 0.0 (a NaN is not).
//
// Still the game's, at their addresses: the adjustor thunks 0x005275A0 to
// 0x00527680 (`sub ecx,N; jmp`, which reach our QueryInterface, AddRef and
// Release); the shared 0x005DEB40 and 0x0041D4C0 of the +8 and +0x10 parts;
// the hash set helpers (SC4Uint32HashSet's construct, clear and resize, the
// ref hash set clear 0x00526050, the ref value map clear 0x007D1B40,
// SC4HashTable::Begin) and the vector helpers (SC4RefPtrVector::InsertN,
// SC4RefVector::Erase and Destroy); the script registrations; the overlay
// manager's 0x0046B4E0, 0x0046D3A0 and Remove; and the rest of the class
// (later parts), among them slot 38 and the two SC4UIScale sites 0x00528580
// and 0x0052CC50.
#include "core/automata_situations.h"

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

namespace opensc4::automata_situations {
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
    log::Write("automata situations: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

void AddRef(void* object) { Virtual<std::uint32_t>(object, slot::kAddRef); }
void Release(void* object) { Virtual<std::uint32_t>(object, slot::kRelease); }

// A referenced field: zeroed, then the old object released.
void Drop(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(object);
    }
}

// A referenced field set to `object`: unless it is already there, the new
// one referenced, stored, and the old one released.
void Assign(char* self, std::size_t offset, void* object)
{
    void* const old = At<void*>(self, offset);
    if (object == old)
        return;
    if (object)
        AddRef(object);
    At<void*>(self, offset) = object;
    if (old)
        Release(old);
}

// The game's SC4String on the stack (vtable 0x00A80810, the STL-style
// begin, end and capacity, a fifth word).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

// A hash map iterator, as SC4HashTable::Begin (0x004B0390) writes it.
struct Iterator {
    char* node;
    char* table;
};

std::uint32_t BucketCount(char* table)
{
    // (end - begin) >> 2, an arithmetic shift as the asm's SAR.
    const std::int32_t bytes = static_cast<std::int32_t>(At<char*>(table, map::kEnd) - At<char*>(table, map::kBegin));
    return static_cast<std::uint32_t>(bytes >> 2);
}

char* Bucket(char* table, std::uint32_t index) { return At<char**>(table, map::kBegin)[index]; }

// The node after `node` in the bucket order, as the iterator's increment
// inlined in the functions here finds it: its chain's next, else the first
// node of a later bucket, starting after its key's bucket.
char* Next(char* node, char* table)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    const std::uint32_t bucket = At<std::uint32_t>(node, 4) % BucketCount(table);
    for (std::uint32_t i = bucket + 1; i < BucketCount(table); ++i)
        if (char* const found = Bucket(table, i))
            return found;
    return nullptr;
}

Iterator Begin(char* table)
{
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, table, static_cast<void*>(&it));
    return it;
}

// The interface Init and Shutdown hand to the script registrations: the
// framework's (0xEBE0E860, 0xE898ED03) into a holder, or none.
void* ScriptHolder(void*& holder)
{
    holder = nullptr;
    if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
        Virtual<std::uint32_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, static_cast<void**>(&holder));
    return holder;
}

// The ten messages Init registers the message target (+4) for, in order.
constexpr std::uint32_t kMessages[] = {0x99EF1142u, 0x99EF1143u, 0x6BB350E4u, 0xEBB67EF7u, 0x8A09FA64u,
                                       0x8A09FA65u, 0x8A09FA67u, 0xCC0D905Fu, 0xAC1045EFu, 0x26D31EC1u};

constexpr std::uint32_t kCheatId = 0x8C4136DBu;

// ====================================================================================
// The object

// 0x005148C0, the factory (cdecl): a 0xB00-byte block and the constructor.
void* __cdecl Create()
{
    static bool logged;
    FirstCall("Create", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(kObjectSize));
    if (!block)
        return nullptr;
    return CallThis<void*>(addr::cSC4CitySituationManager_Construct, block);
}

void ConstructMap(char* self, std::size_t mapField)
{
    char* const table = self + mapField;
    At<std::uint32_t>(table, 0x04) = 0;
    At<std::uint32_t>(table, 0x08) = 0;
    At<std::uint32_t>(table, 0x0C) = 0;
    At<std::uint32_t>(table, 0x10) = 0;
    CallThis<void>(addr::SC4Uint32HashSet_Construct, table, static_cast<std::uint32_t>(100));
}

// 0x00529FD0, the constructor.
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, field::kVtable8) = addr::SC4Audio_OldTargetBaseVtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, field::kVtable10) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4CitySituationManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4CitySituationManager_Vtable4;
    At<std::uintptr_t>(self, field::kVtable8) = addr::cSC4CitySituationManager_Vtable8;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4CitySituationManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kVtable10) = addr::cSC4CitySituationManager_Vtable10;
    At<std::uint8_t>(self, field::kInitialized) = 0;
    At<std::uint32_t>(self, field::kRefCount) = 0;
    CallThis<void*>(addr::cRZDate_DefaultBase, self + field::kDate);
    At<std::uint32_t>(self, field::kScriptContext) = 0;
    CallThis<void*>(addr::SC4Random_ConstructSeeded, self + field::kRandom, 0xFFFFFFFFu);
    At<std::uint8_t>(self, field::kCsiVisible) = 1;
    At<std::uint8_t>(self, field::kRead) = 0;
    At<std::uint32_t>(self, field::kSituation) = 0;
    At<std::uint32_t>(self, field::kServices) = 0;
    At<std::uint32_t>(self, field::kServices + 4) = 0;
    At<std::uint32_t>(self, field::kServices + 8) = 0;
    At<std::uint32_t>(self, field::kList) = 0;
    char* const head = static_cast<char*>(Allocate(0x14));
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    At<char*>(self, field::kList) = head;
    ConstructMap(self, field::kTracked);
    for (std::size_t at = field::kRefA1C; at <= field::kZeroA30; at += 4)
        At<std::uint32_t>(self, at) = 0;
    ConstructMap(self, field::kSuccesses);
    At<std::uint32_t>(self, field::kStatus) = 4;
    for (std::size_t at = field::kAuto; at <= field::kTargetIndex; at += 4)
        At<std::uint32_t>(self, at) = 0;
    At<std::uint8_t>(self, field::kByteA64) = 0;
    ConstructMap(self, field::kRefSet);
    for (std::size_t at = field::kZeroA7C; at <= field::kTimeLimit; at += 4)
        At<std::uint32_t>(self, at) = 0;
    At<std::uint8_t>(self, field::kJoyride) = 0;
    At<std::uint8_t>(self, field::kByteA99) = 0;
    At<std::uint32_t>(self, field::kRefA9C) = 0;
    At<std::uint32_t>(self, field::kRefAA0) = 0;
    At<std::uint32_t>(self, field::kRefAA4) = 0;
    ConstructMap(self, field::kSetAA8);
    At<std::uint32_t>(self, field::kZeroABC) = 0;
    At<std::uint32_t>(self, field::kZeroAC0) = 0;
    ConstructMap(self, field::kObjects);
    for (std::size_t at = field::kWordsADC; at < field::kRefAF8; at += 4)
        At<std::uint32_t>(self, at) = 0xFFFFFFFEu;
    At<std::uint32_t>(self, field::kRefAF8) = 0;
    At<std::uint32_t>(self, field::kZeroAFC) = 0;
    return self;
}

// A hash map's clear and its bucket vector's buffer, as the destructor
// frees them.
void DestroyMap(char* self, std::size_t mapField, std::uintptr_t clear)
{
    char* const table = self + mapField;
    CallThis<void>(clear, table);
    if (void* const buckets = At<void*>(table, map::kBegin))
        Free(buckets);
}

void ReleaseIfSet(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset))
        Release(object);
}

// 0x00527320, the destructor body (the deleting destructor calls it).
void __fastcall Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Destroy", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4CitySituationManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4CitySituationManager_Vtable4;
    At<std::uintptr_t>(self, field::kVtable8) = addr::cSC4CitySituationManager_Vtable8;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4CitySituationManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kVtable10) = addr::cSC4CitySituationManager_Vtable10;
    ReleaseIfSet(self, field::kRefAF8);
    DestroyMap(self, field::kObjects, addr::SC4RefValueHashMap_Clear);
    DestroyMap(self, field::kSetAA8, addr::SC4Uint32HashSet_Clear);
    ReleaseIfSet(self, field::kRefAA4);
    ReleaseIfSet(self, field::kRefAA0);
    ReleaseIfSet(self, field::kRefA9C);
    DestroyMap(self, field::kRefSet, addr::SC4RefHashSet_Clear);
    ReleaseIfSet(self, field::kRefA58);
    ReleaseIfSet(self, field::kRefA54);
    ReleaseIfSet(self, field::kTarget);
    ReleaseIfSet(self, field::kAuto);
    DestroyMap(self, field::kSuccesses, addr::SC4Uint32HashSet_Clear);
    ReleaseIfSet(self, field::kOverlay);
    ReleaseIfSet(self, field::kRefA28);
    ReleaseIfSet(self, field::kCity71);
    ReleaseIfSet(self, field::kLotManager);
    ReleaseIfSet(self, field::kRefA1C);
    DestroyMap(self, field::kTracked, addr::SC4Uint32HashSet_Clear);
    CallThis<void>(addr::cSC4CitySituationManager_ClearList, self + field::kList);
    if (void* const head = At<void*>(self, field::kList))
        Free(head);
    CallThis<void>(addr::SC4RefVector_Destroy, self + field::kServices);
    ReleaseIfSet(self, field::kSituation);
    ReleaseIfSet(self, field::kScriptContext);
}

// 0x00527AA0, slot 34 (flags), ret 4: the destructor body, and the block
// freed for flag 1.
char* __fastcall DeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4CitySituationManager_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x005246B0, slot 0 (iid, out), ret 8. The +4, +8, +0xC and +0x10 parts
// reach it through the adjustor thunks 0x005275A0, 0x005275C0, 0x005275E0
// and 0x00527600.
std::uint8_t __fastcall QueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    char* volatile object = self; // the game calls through it without a test
    std::size_t part;
    if (iid == kManagerIid) {
        *out = object;
        Virtual<std::uint32_t>(object, slot::kAddRef);
        return 1;
    }
    if (iid == 1 || iid == 0xE4FDA3D4u)
        part = field::kSerializable;
    else if (iid == 0x452294AAu)
        part = field::kVtable4;
    else if (iid == 0xE98B2F57u)
        part = field::kVtable8;
    else
        return 0;
    *out = object ? object + part : nullptr;
    Virtual<std::uint32_t>(object, slot::kAddRef);
    return 1;
}

// 0x00524750, slot 1: the count (+0x18) up, answered.
std::uint32_t __fastcall AddRefOurs(char* self, void*)
{
    static bool logged;
    FirstCall("AddRef", logged);
    return ++At<std::uint32_t>(self, field::kRefCount);
}

// 0x00525520, slot 2: a count above 1 counted down and answered; else
// Shutdown (slot 4) and the deleting destructor (slot 34) with 1, answer 0.
std::uint32_t __fastcall ReleaseOurs(char* self, void*)
{
    static bool logged;
    FirstCall("Release", logged);
    const std::uint32_t count = At<std::uint32_t>(self, field::kRefCount);
    if (count > 1) {
        At<std::uint32_t>(self, field::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<std::uint8_t>(self, slot::kShutdown);
    Virtual<void*>(self, slot::kDeletingDestructor, 1u);
    return 0;
}

// 0x005274E0, the serializable part's slot 5: the class ID.
std::uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// The life cycle

// 0x0052DC40, slot 3.
std::uint8_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    void* const target = self + field::kVtable4;
    if (Virtual<std::uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
        if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
            Virtual<void>(cheats, 0x1C, target, 0u);
            GameString text;
            CallThis<void*>(addr::SC4String_FromText, static_cast<void*>(&text),
                            Global<const char*>(addr::cSC4CitySituationManager_CheatName));
            Virtual<void>(cheats, 0x0C, kCheatId, static_cast<void*>(&text));
            text.vtable = addr::SC4String_Vtable;
            if (text.begin)
                Free(text.begin);
        }
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const std::uint32_t message : kMessages)
            Virtual<void>(server, 0x14, target, message);
    if (void* const dispatch = Global<void*>(addr::SC4_DispatchService))
        Assign(self, field::kOverlay, Virtual<void*>(dispatch, 0x58));
    if (void* const service = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<void>(service, 0x14, target, 0xFFu);
    // The four services, each pushed onto the vector +0x9F8 (an inlined
    // push_back: a null end is stepped past without a store).
    for (const std::uintptr_t global :
         {addr::SC4_Service43D44, addr::SC4_Service43D4C, addr::SC4_Service43D48, addr::SC4_Service43D50}) {
        void* service = Global<void*>(global);
        if (service)
            AddRef(service);
        char*& end = At<char*>(self, field::kServices + 4);
        if (end != At<char*>(self, field::kServices + 8)) {
            if (end) {
                *reinterpret_cast<void**>(end) = service;
                if (service)
                    AddRef(service);
            }
            end += 4;
        } else {
            std::uint8_t allocator = 0;
            CallThis<void>(addr::SC4RefPtrVector_InsertN, self + field::kServices, static_cast<void*>(end),
                           static_cast<void*>(&service), static_cast<void*>(&allocator), 1u, 1u);
        }
        if (service)
            Release(service);
    }
    Assign(self, field::kLotManager, Global<void*>(addr::SC4_LotManager));
    Assign(self, field::kCity71, Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C));
    Assign(self, field::kScriptContext, Global<void*>(addr::SC4_ScriptContext));
    void* holder = nullptr;
    CallCdecl<void>(addr::SC4Script_RegisterTable, static_cast<void*>(self), addr::cSC4CitySituationManager_TableName,
                    0u, 0u, 0u, ScriptHolder(holder));
    CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    struct Function {
        std::uintptr_t callback;
        std::uintptr_t name;
    };
    auto registerFunction = [&](const Function& f) {
        CallCdecl<void>(addr::SC4Script_RegisterFunction, f.callback, f.name, f.callback, static_cast<void*>(self), 0u,
                        static_cast<void*>(self), ScriptHolder(holder));
        CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    };
    auto registerVariable = [&](std::size_t offset, std::uintptr_t name) {
        CallCdecl<void>(addr::SC4Script_RegisterVariable, static_cast<void*>(self + offset), name,
                        addr::cSC4CitySituationManager_LuaVariable, static_cast<void*>(self), 2u, 0u,
                        ScriptHolder(holder));
        CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    };
    registerFunction({addr::cSC4CitySituationManager_LuaGetSuccessCount,
                      addr::cSC4CitySituationManager_GetSuccessCountText});
    registerFunction({addr::cSC4CitySituationManager_LuaActivateSituation,
                      addr::cSC4CitySituationManager_ActivateSituationText});
    registerFunction({addr::cSC4CitySituationManager_LuaGetCurrentTarget,
                      addr::cSC4CitySituationManager_GetCurrentTargetText});
    registerFunction({addr::cSC4CitySituationManager_LuaGetActiveAuto,
                      addr::cSC4CitySituationManager_GetActiveAutoText});
    registerVariable(field::kNumTargets, addr::cSC4CitySituationManager_NumTargetsText);
    registerVariable(field::kTargetIndex, addr::cSC4CitySituationManager_CurTargetIndexText);
    registerFunction({addr::cSC4CitySituationManager_LuaGetTargetName,
                      addr::cSC4CitySituationManager_GetTargetNameText});
    registerFunction({addr::cSC4CitySituationManager_LuaShowCsi, addr::cSC4CitySituationManager_ShowCsiText});
    registerFunction({addr::cSC4CitySituationManager_LuaNumTargetsRemaining,
                      addr::cSC4CitySituationManager_NumTargetsRemainingText});
    Virtual<std::uint8_t>(self, slot::kHandleTuningParametersChanged);
    if (void* const overlay = At<void*>(self, field::kOverlay))
        CallThis<void>(addr::SC4OverlayManager_F46D3A0, overlay, 4u,
                       static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kCsiVisible)));
    // The tutorials the preferences hold as done (+0x1334, zero ended), each
    // with a success count of 1.
    if (void* const app = Global<void*>(addr::SC4_Service43C94))
        if (char* const preferences = Virtual<char*>(app, 0x98))
            for (std::uint32_t* id = reinterpret_cast<std::uint32_t*>(preferences + 0x1334); *id; ++id)
                *CallThis<std::uint16_t*>(addr::cSC4CitySituationManager_SuccessAt, self + field::kSuccesses, id) = 1;
    Drop(self, field::kRefAF8);
    At<std::uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// 0x00527110 (key), ret 4, this = the success map (+0xA34): the address of
// the key's uint16; a key not there is inserted with 0 (0x005264A0).
std::uint16_t* __fastcall SuccessAt(char* table, void*, const std::uint32_t* key)
{
    static bool logged;
    FirstCall("SuccessAt", logged);
    const std::uint32_t k = *key;
    for (char* node = Bucket(table, k % BucketCount(table)); node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, 4) == k)
            return &At<std::uint16_t>(node, 8);
    struct Pair {
        std::uint32_t key;
        std::uint16_t value;
        std::uint16_t pad;
    } pair{k, 0, 0};
    char* const inserted =
        CallThis<char*>(addr::cSC4CitySituationManager_SuccessInsert, table, static_cast<const void*>(&pair));
    return reinterpret_cast<std::uint16_t*>(inserted + 4);
}

// 0x005264A0 (pair), ret 4, this = the success map: the buckets grown for one
// more (0x004F0940), a new node (0xC bytes) with the pair at the head of its
// key's bucket, the count up; answers the node's pair (node + 4).
char* __fastcall SuccessInsert(char* table, void*, const void* pair)
{
    static bool logged;
    FirstCall("SuccessInsert", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Resize, table, At<std::uint32_t>(table, map::kCount) + 1);
    const std::uint32_t bucket = At<std::uint32_t>(const_cast<void*>(pair), 0) % BucketCount(table);
    char* const old = Bucket(table, bucket);
    char* const node = static_cast<char*>(Allocate(0xC));
    At<char*>(node, 0) = nullptr;
    char* const value = node + 4;
    if (value) {
        At<std::uint32_t>(value, 0) = At<std::uint32_t>(const_cast<void*>(pair), 0);
        At<std::uint16_t>(value, 4) = At<std::uint16_t>(const_cast<void*>(pair), 4);
    }
    At<char*>(node, 0) = old;
    At<char**>(table, map::kBegin)[bucket] = node;
    ++At<std::uint32_t>(table, map::kCount);
    return value;
}

// 0x00527AC0, slot 4.
std::uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("Shutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    void* const target = self + field::kVtable4;
    Drop(self, field::kRefAF8);
    Drop(self, field::kCity71);
    Drop(self, field::kScriptContext);
    Drop(self, field::kLotManager);
    Drop(self, field::kRefA9C);
    Drop(self, field::kRefA28);
    Drop(self, field::kRefAA0);
    CallThis<void>(addr::cSC4CitySituationManager_ReleaseObjects, self);
    char* const vector = self + field::kServices;
    for (std::uint32_t i = 0;
         i < static_cast<std::uint32_t>(static_cast<std::int32_t>(At<char*>(vector, 4) - At<char*>(vector, 0)) >> 2);
         ++i) {
        void*& entry = At<void**>(vector, 0)[i];
        if (void* const service = entry) {
            entry = nullptr;
            Release(service);
        }
    }
    CallThis<void>(addr::SC4RefVector_Erase, vector, At<void*>(vector, 0), At<void*>(vector, 4));
    if (void* const service = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<void>(service, 0x18, target, 0xFFu);
    Drop(self, field::kOverlay);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const std::uint32_t message : kMessages)
            Virtual<void>(server, 0x18, target, message);
    Virtual<void>(self, slot::kEndSituation);
    CallThis<void>(addr::cSC4CitySituationManager_ReleaseTracked, self);
    CallThis<void>(addr::cSC4CitySituationManager_ReleaseList, self);
    void* holder = nullptr;
    if (void* const script = ScriptHolder(holder)) {
        void* unknown = nullptr;
        Virtual<std::uint8_t>(self, slot::kQueryInterface, 1u, &unknown);
        Virtual<void>(script, 0x1C, unknown);
        if (unknown)
            Release(unknown);
    }
    CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    if (Virtual<std::uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
        if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
            Virtual<void>(cheats, 0x20, target, 0u);
            Virtual<void>(cheats, 0x10, kCheatId);
        }
    }
    At<std::uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// 0x00527800: each object of the map +0xAC4 told to stop (its slot 4 with
// 1, no null test) and released (zeroed first), in bucket order; then the
// map cleared (0x007D1B40).
void __fastcall ReleaseObjects(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseObjects", logged);
    char* const table = self + field::kObjects;
    const Iterator it = Begin(table);
    for (char* node = it.node; node; node = Next(node, it.table)) {
        Virtual<void>(At<void*>(node, 8), 0x10, 1u);
        if (void* const object = At<void*>(node, 8)) {
            At<void*>(node, 8) = nullptr;
            Release(object);
        }
    }
    CallThis<void>(addr::SC4RefValueHashMap_Clear, table);
}

// 0x00526E60: each tracked automaton of the set +0xA08 released (the key,
// not zeroed), in bucket order; then the set cleared.
void __fastcall ReleaseTracked(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseTracked", logged);
    char* const table = self + field::kTracked;
    const Iterator it = Begin(table);
    for (char* node = it.node; node; node = Next(node, it.table))
        if (void* const automaton = At<void*>(node, 4))
            Release(automaton);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, table);
}

// 0x005263D0: per node of the list +0xA04, +8 dropped, the overlay ID
// (+0x10) taken off the overlay manager when both are there, +0xC dropped
// and the ID zeroed; then the list cleared (a tail jump to 0x00525EC0).
void __fastcall ReleaseList(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseList", logged);
    char* const head = At<char*>(self, field::kList);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        Drop(node, 0x08);
        if (void* const overlay = At<void*>(self, field::kOverlay))
            if (const std::uint32_t id = At<std::uint32_t>(node, 0x10))
                CallThis<void>(addr::SC4OverlayManager_Remove, overlay, id, 0u);
        Drop(node, 0x0C);
        At<std::uint32_t>(node, 0x10) = 0;
    }
    CallThis<void>(addr::cSC4CitySituationManager_ClearList, self + field::kList);
}

// 0x00525EC0, this = the list's field: each node's +0xC and +8 released (not
// zeroed) and the node freed; the head linked to itself.
void __fastcall ClearList(char** list, void*)
{
    static bool logged;
    FirstCall("ClearList", logged);
    char* node = At<char*>(*list, 0);
    while (node != *list) {
        char* const next = At<char*>(node, 0);
        if (void* const a = At<void*>(node, 0x0C))
            Release(a);
        if (void* const b = At<void*>(node, 0x08))
            Release(b);
        Free(node);
        node = next;
    }
    At<char*>(*list, 0) = *list;
    At<char*>(*list, 4) = *list;
}

// ====================================================================================
// The queries and setters

// 0x00526320, slot 5 (id), ret 4: the success map's (+0xA34) uint16 for the
// ID, or 0, in AX; the high half is the bucket vector's address's.
std::uint32_t __fastcall GetMissionSuccessCount(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("GetMissionSuccessCount", logged);
    char* const table = self + field::kSuccesses;
    const std::uint32_t high = reinterpret_cast<std::uintptr_t>(At<char*>(table, map::kBegin)) & 0xFFFF0000u;
    for (char* node = Bucket(table, id % BucketCount(table)); node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, 4) == id)
            return high | At<std::uint16_t>(node, 8);
    return high;
}

// 0x00527620, slot 6: the active situation (+0x9F4).
void* __fastcall GetActiveSituation(char* self, void*)
{
    static bool logged;
    FirstCall("GetActiveSituation", logged);
    return At<void*>(self, field::kSituation);
}

// 0x005274F0, slot 7: the status (+0xA48).
std::uint32_t __fastcall GetCurrentStatus(char* self, void*)
{
    static bool logged;
    FirstCall("GetCurrentStatus", logged);
    return At<std::uint32_t>(self, field::kStatus);
}

// 0x00527630, slot 8: the active automaton (+0xA4C).
void* __fastcall GetActiveAuto(char* self, void*)
{
    static bool logged;
    FirstCall("GetActiveAuto", logged);
    return At<void*>(self, field::kAuto);
}

// 0x00527640, slot 9: the current target (+0xA50).
void* __fastcall GetCurrentTarget(char* self, void*)
{
    static bool logged;
    FirstCall("GetCurrentTarget", logged);
    return At<void*>(self, field::kTarget);
}

// 0x00527510, slot 10: the current target's index (+0xA60).
std::uint32_t __fastcall GetCurrentTargetIndex(char* self, void*)
{
    static bool logged;
    FirstCall("GetCurrentTargetIndex", logged);
    return At<std::uint32_t>(self, field::kTargetIndex);
}

// 0x00527520, slot 11: the target count (+0xA5C).
std::uint32_t __fastcall GetNumTargets(char* self, void*)
{
    static bool logged;
    FirstCall("GetNumTargets", logged);
    return At<std::uint32_t>(self, field::kNumTargets);
}

// 0x00527530, slot 12: slot 11 less slot 10, through the vtable.
std::uint32_t __fastcall GetNumTargetsRemaining(char* self, void*)
{
    static bool logged;
    FirstCall("GetNumTargetsRemaining", logged);
    const std::uint32_t count = Virtual<std::uint32_t>(self, slot::kGetNumTargets);
    return count - Virtual<std::uint32_t>(self, slot::kGetCurrentTargetIndex);
}

// 0x00527550, slot 13: the time limit (+0xA94).
float __fastcall GetCurrentTimeLimitSeconds(char* self, void*)
{
    static bool logged;
    FirstCall("GetCurrentTimeLimitSeconds", logged);
    return At<float>(self, field::kTimeLimit);
}

// 0x00527560, slot 14: the situation's time (+0xA8C).
float __fastcall GetCurrentSituationTime(char* self, void*)
{
    static bool logged;
    FirstCall("GetCurrentSituationTime", logged);
    return At<float>(self, field::kSituationTime);
}

// 0x00527570, slot 15: slot 13 (through the vtable) less the situation's
// time, on the x87 stack (not rounded to a float).
double __fastcall GetCurrentTimeRemainingSeconds(char* self, void*)
{
    static bool logged;
    FirstCall("GetCurrentTimeRemainingSeconds", logged);
    const double limit = Virtual<double>(self, slot::kGetCurrentTimeLimitSeconds);
    return limit - static_cast<double>(At<float>(self, field::kSituationTime));
}

// 0x005290F0, slot 16 (out), ret 4: the current target's name into the
// SC4String at out (constructed here), answered: an empty string (an
// 8-byte buffer), assigned (its slot 13) the target's name (the
// 0x87DDA3A9 interface's slot 12) when there is a target with that
// interface, then copied into out (0x004089B0) and destroyed.
GameString* __fastcall GetCurrentTargetName(char* self, void*, GameString* out)
{
    static bool logged;
    FirstCall("GetCurrentTargetName", logged);
    GameString name;
    name.begin = nullptr;
    name.end = nullptr;
    name.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    name.capacity = buffer + 8;
    name.begin = buffer;
    name.end = buffer;
    *buffer = 0;
    name.vtable = addr::SC4String_Vtable;
    name.extra = 0;
    if (void* const target = At<void*>(self, field::kTarget)) {
        void* face = nullptr;
        if (Virtual<std::uint8_t>(target, 0x00, 0x87DDA3A9u, &face)) {
            void* const text = Virtual<void*>(face, 0x30);
            Virtual<void>(static_cast<void*>(&name), 0x34, text);
        }
        if (face)
            Release(face);
    }
    void* allocator = out;
    CallThis<void*>(addr::SC4String_CopyBuffer, static_cast<void*>(&out->begin), static_cast<const void*>(&name.begin),
                    0u, 0xFFFFFFFFu, static_cast<void*>(&allocator));
    out->vtable = addr::SC4String_Vtable;
    out->extra = 0;
    name.vtable = addr::SC4String_Vtable;
    if (name.begin)
        Free(name.begin);
    return out;
}

// 0x00524B70, slot 17 (a, b), ret 8: without a situation AL 0; else the
// situation's slot 49 (status 2) or slot 46 with (a, b), AL 1.
std::uint8_t __fastcall GetSituationAura(char* self, void*, void* a, void* b)
{
    static bool logged;
    FirstCall("GetSituationAura", logged);
    void* const situation = At<void*>(self, field::kSituation);
    if (!situation)
        return 0;
    Virtual<void>(situation, At<std::uint32_t>(self, field::kStatus) == 2 ? 0xC4 : 0xB8, a, b);
    return 1;
}

// 0x00524BB0, slot 18 (money), ret 4: without a situation AL 0; else the
// situation's slot 51 (status 2) or slot 48 with the pointer, AL 1.
std::uint8_t __fastcall GetSituationMoney(char* self, void*, void* money)
{
    static bool logged;
    FirstCall("GetSituationMoney", logged);
    void* const situation = At<void*>(self, field::kSituation);
    if (!situation)
        return 0;
    Virtual<void>(situation, At<std::uint32_t>(self, field::kStatus) == 2 ? 0xCC : 0xC0, money);
    return 1;
}

// 0x00524BF0, slot 26 (value), ret 4: the most CSIs (the global 0x00B21D34),
// and the overlay manager's (+0xA2C) layer 4 value (0x0046B4E0) when there is
// one.
void __fastcall SetMaxCsi(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("SetMaxCsi", logged);
    const std::uint8_t most = static_cast<std::uint8_t>(value);
    *reinterpret_cast<std::uint8_t*>(addr::cSC4CitySituationManager_MaxCsi) = most;
    if (void* const overlay = At<void*>(self, field::kOverlay))
        CallThis<void>(addr::SC4OverlayManager_SetLayerValue, overlay, 4u, static_cast<std::uint32_t>(most));
}

// 0x005245C0, slot 27: the most CSIs.
std::uint8_t __fastcall GetMaxCsi(void*, void*)
{
    static bool logged;
    FirstCall("GetMaxCsi", logged);
    return Global<std::uint8_t>(addr::cSC4CitySituationManager_MaxCsi);
}

// 0x00527690, slot 28: the tracked automata's count (+0xA18).
std::uint32_t __fastcall GetTrackedAutomataCount(char* self, void*)
{
    static bool logged;
    FirstCall("GetTrackedAutomataCount", logged);
    return At<std::uint32_t>(self + field::kTracked, map::kCount);
}

// 0x00524C20, slot 29 (visible), ret 4: +0x9F0, and the overlay manager's
// 0x0046D3A0 with (4, the argument's whole word) when there is one.
void __fastcall SetCsiVisible(char* self, void*, std::uint32_t visible)
{
    static bool logged;
    FirstCall("SetCsiVisible", logged);
    At<std::uint8_t>(self, field::kCsiVisible) = static_cast<std::uint8_t>(visible);
    if (void* const overlay = At<void*>(self, field::kOverlay))
        CallThis<void>(addr::SC4OverlayManager_F46D3A0, overlay, 4u, visible);
}

// 0x00527500, slot 30: +0x9F0.
std::uint8_t __fastcall GetCsiVisible(char* self, void*)
{
    static bool logged;
    FirstCall("GetCsiVisible", logged);
    return At<std::uint8_t>(self, field::kCsiVisible);
}

// 0x005245D0, slot 31: SetMaxCsi (slot 26, through the vtable) with the most
// CSIs; AL 1.
std::uint8_t __fastcall HandleTuningParametersChanged(char* self, void*)
{
    static bool logged;
    FirstCall("HandleTuningParametersChanged", logged);
    Virtual<void>(self, slot::kSetMaxCsi,
                  static_cast<std::uint32_t>(Global<std::uint8_t>(addr::cSC4CitySituationManager_MaxCsi)));
    return 1;
}

// 0x00527580, slot 32: whether the countdown (+0xA90) is above 0.0 (the
// float at 0x00A81054), as a word.
std::uint32_t __fastcall CountdownIsInProgress(char* self, void*)
{
    static bool logged;
    FirstCall("CountdownIsInProgress", logged);
    return At<float>(self, field::kCountdown) > Global<float>(addr::SC4_FloatZero) ? 1u : 0u;
}

// 0x005266E0, slot 33: the success map cleared, and with the app's
// preferences (its slot 38) the done tutorials (+0x1334, 0x100 bytes)
// zeroed and the preferences saved (a tail call of the app's slot 17).
void __fastcall ResetTutorials(char* self, void*)
{
    static bool logged;
    FirstCall("ResetTutorials", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + field::kSuccesses);
    void* const app = Global<void*>(addr::SC4_Service43C94);
    if (!app)
        return;
    char* const preferences = Virtual<char*>(app, 0x98);
    if (!preferences)
        return;
    std::memset(preferences + 0x1334, 0, 0x100);
    Virtual<std::uint8_t>(app, 0x44);
}

// ====================================================================================
// The save (this = the serializable part, the object + 0xC)

// 0x00527040 (stream), ret 4: the version 2 (a word), the success map's
// count and per entry its ID and uint16, in bucket order, then the CSI's
// visibility (SC4Stream::WriteBool); answers 1 when the stream has no error
// (its slot 18 AL 0).
std::uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    char* const table = self + field::kSuccesses;
    // The words go out with the high half of the part's address above them
    // (the register the asm loads each word into held it).
    std::uint32_t word = reinterpret_cast<std::uintptr_t>(part);
    Virtual<void>(stream, 0x1C, 2u);
    Virtual<void>(stream, 0x24, At<std::uint32_t>(table, map::kCount));
    const Iterator it = Begin(table);
    for (char* node = it.node; node; node = Next(node, it.table)) {
        word = (word & 0xFFFF0000u) | At<std::uint16_t>(node, 8);
        Virtual<void>(stream, 0x24, At<std::uint32_t>(node, 4));
        Virtual<void>(stream, 0x1C, word);
    }
    CallCdecl<void>(addr::SC4Stream_WriteBool, stream,
                    static_cast<std::uint32_t>(At<std::uint8_t>(self, field::kCsiVisible)));
    return Virtual<std::uint8_t>(stream, 0x48) ? 0u : 1u;
}

// 0x00529010 (stream), ret 4: the version (a word, read into the argument's
// own slot); above 2 AL 0. Else +0x9F1 set, the success map cleared, its
// count and entries read (each inserted by 0x00525F10 after the buckets grow
// for one more), the CSI's visibility for a version above 1; answers 1 when
// the stream has no error.
std::uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    // The game reads the version over its stream argument's low word.
    std::uint32_t slot = reinterpret_cast<std::uintptr_t>(stream);
    Virtual<void>(stream, 0x1C, static_cast<void*>(&slot));
    const std::uint16_t version = static_cast<std::uint16_t>(slot);
    if (version > 2)
        return 0;
    char* const table = self + field::kSuccesses;
    At<std::uint8_t>(self, field::kRead) = 1;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, table);
    std::uint32_t count = 0;
    Virtual<void>(stream, 0x24, static_cast<void*>(&count));
    // The key and the value are the game's locals across the entries: a read
    // that fails leaves the previous entry's.
    std::uint32_t key = 0;
    std::uint16_t value = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        Virtual<void>(stream, 0x24, static_cast<void*>(&key));
        Virtual<void>(stream, 0x1C, static_cast<void*>(&value));
        struct Pair {
            std::uint32_t key;
            std::uint16_t value;
            std::uint16_t pad;
        } pair{key, value, 0};
        CallThis<void>(addr::SC4Uint32HashSet_Resize, table, At<std::uint32_t>(table, map::kCount) + 1);
        struct Result {
            void* node;
            void* table;
            std::uint8_t inserted;
            std::uint8_t pad[3];
        } result{};
        CallThis<void*>(addr::cSC4CitySituationManager_SuccessInsertUnique, table, static_cast<void*>(&result),
                        static_cast<const void*>(&pair));
    }
    if (version > 1)
        CallCdecl<void>(addr::SC4Stream_ReadBool, stream, static_cast<void*>(self + field::kCsiVisible));
    return Virtual<std::uint8_t>(stream, 0x48) ? 0u : 1u;
}

// 0x00525F10 (result, pair), ret 8, this = the success map: the node of the
// pair's key into result (node, map, 0) when it is there; else a new node
// (0xC bytes) with the pair at the head of its bucket (no growing here), the
// count up, and (node, map, 1).
void* __fastcall SuccessInsertUnique(char* table, void*, char* result, const void* pair)
{
    static bool logged;
    FirstCall("SuccessInsertUnique", logged);
    const std::uint32_t key = At<std::uint32_t>(const_cast<void*>(pair), 0);
    const std::uint32_t bucket = key % BucketCount(table);
    char* const first = Bucket(table, bucket);
    for (char* node = first; node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, 4) == key) {
            At<char*>(result, 4) = table;
            At<char*>(result, 0) = node;
            At<std::uint8_t>(result, 8) = 0;
            return result;
        }
    char* const node = static_cast<char*>(Allocate(0xC));
    At<char*>(node, 0) = nullptr;
    char* const value = node + 4;
    if (value) {
        At<std::uint32_t>(value, 0) = key;
        At<std::uint16_t>(value, 4) = At<std::uint16_t>(const_cast<void*>(pair), 4);
    }
    At<char*>(node, 0) = first;
    At<char**>(table, map::kBegin)[bucket] = node;
    ++At<std::uint32_t>(table, map::kCount);
    At<char*>(result, 4) = table;
    At<char*>(result, 0) = node;
    At<std::uint8_t>(result, 8) = 1;
    return result;
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
            log::Write("automata situations %s: no address entry for 0x%08X", what,
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
    log::Write("automata situations %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Create, Create),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_AddRef, AddRefOurs),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Release, ReleaseOurs),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetGZCLSID, GetGZCLSID),
};

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Init, Init),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SuccessAt, SuccessAt),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SuccessInsert, SuccessInsert),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ReleaseObjects, ReleaseObjects),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ReleaseTracked, ReleaseTracked),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ReleaseList, ReleaseList),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ClearList, ClearList),
};

const Detour kQueries[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetMissionSuccessCount, GetMissionSuccessCount),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetActiveSituation, GetActiveSituation),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentStatus, GetCurrentStatus),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetActiveAuto, GetActiveAuto),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentTarget, GetCurrentTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentTargetIndex, GetCurrentTargetIndex),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetNumTargets, GetNumTargets),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetNumTargetsRemaining, GetNumTargetsRemaining),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentTimeLimitSeconds, GetCurrentTimeLimitSeconds),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentSituationTime, GetCurrentSituationTime),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentTimeRemainingSeconds, GetCurrentTimeRemainingSeconds),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCurrentTargetName, GetCurrentTargetName),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetSituationAura, GetSituationAura),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetSituationMoney, GetSituationMoney),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SetMaxCSI, SetMaxCsi),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetMaxCSI, GetMaxCsi),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetTrackedAutomataCount, GetTrackedAutomataCount),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SetCSIVisible, SetCsiVisible),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_GetCSIVisible, GetCsiVisible),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_HandleTuningParametersChanged, HandleTuningParametersChanged),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CountdownIsInProgress, CountdownIsInProgress),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ResetTutorials, ResetTutorials),
};

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Write, Write),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Read, Read),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SuccessInsertUnique, SuccessInsertUnique),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode) { return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object"); }
bool InstallLife(registry::Mode) { return InstallList(kLife, sizeof kLife / sizeof kLife[0], "life"); }
bool InstallQueries(registry::Mode) { return InstallList(kQueries, sizeof kQueries / sizeof kQueries[0], "queries"); }
bool InstallSave(registry::Mode) { return InstallList(kSave, sizeof kSave / sizeof kSave[0], "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.situations.object", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_Construct,
                        "Our city situation manager, the object: the factory, the constructor, the destructor body, "
                        "the deleting destructor, QueryInterface, AddRef, Release and the serializable part's "
                        "GetGZCLSID (8 detours; part c1, src/core/automata_situations.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.automata.situations.life", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_Init,
                        "Our city situation manager's life cycle: Init with the success map's operator[] and insert, "
                        "Shutdown with the release of the controlled objects, the tracked automata and the list, and "
                        "the list's clear (8 detours; part c1, src/core/automata_situations.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.automata.situations.queries", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_GetCurrentStatus,
                        "Our city situation manager's queries and setters: slots 5 to 18 (the mission successes, the "
                        "situation, its target, times, aura and money) and 26 to 33 (the CSI, the tracked automata "
                        "count, the tuning, the countdown, the tutorials) (22 detours; part c1, "
                        "src/core/automata_situations.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.automata.situations.save", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_Write,
                        "Our city situation manager's save: Write, Read and the success map's insert (3 detours; part "
                        "c1, src/core/automata_situations.cpp)",
                        false, InstallSave}),
};

} // namespace
} // namespace opensc4::automata_situations
