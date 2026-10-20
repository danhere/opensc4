// Our aircraft and watercraft managers and the base all four automata
// managers share, part aw1 (build 641): the object (the two factories, the
// three constructors, the destructor bodies and the deleting destructors
// with the container clears they call, QueryInterface of the three
// classes, the common base's AddRef and Release, the serializable part's
// GetGZCLSID of the three) and the common base's queries (IsActive, Pause,
// Resume, IsPaused, IsDrivingMode, GetMinZoom, the five-argument Dispatch
// stub, GetExemplarData and its map, CanCreateGroup, IsCameraFollowUpdating,
// GetSourceBuildingCount and its group form, the exemplar find of slot 44).
// The class map is in src/core/automata_awmgr.h and the survey in
// docs/automata.md, "Part aw". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Group, off by default:
//   simulators.automata.awmgr: the 36 detours listed in kObject below.
//
// The common base's functions are also the vehicle and pedestrian managers'
// (part v1 calls the constructor, the destructor and QueryInterface at their
// addresses, and both managers' vtables hold AddRef, Release and the
// queries), so this group changes all four managers.
//
// What it does. The common base's constructor sets the four interim
// vtables of its own bases and then its own, zeroes its fields, makes the
// automata list's head (0xC bytes), the generator at +0x50 (a clock seed),
// the exemplar map's header (0x20 bytes) and the two hash tables (100
// buckets). The aircraft manager adds a map (+0xA88) and a list (+0xA94);
// the watercraft manager three hash tables and a reference vector. The
// factories answer the object + 8, the serializable part. QueryInterface
// answers cISC4AutomataManager (0x098F9BF4) with the object, 1 with +4,
// the serializable part (0xE4FDA3D4) with +8 and the message target
// (0x452294AA) with +0xC; the two derived classes answer their own class ID
// with the object and leave the rest to it.
//
// Oddities kept: QueryInterface's branches for 1, 0xE4FDA3D4 and
// 0x452294AA write a null and still call AddRef through the object when it
// is null, and the cISC4AutomataManager branch never tests it (the address
// is held in a volatile local, as in parts a1 and v1). Release at a count
// of 1 (or 0) shuts down and deletes without counting down. The common
// base's constructor writes the vtables of its own bases (0x00A8298C,
// 0x00A80784, 0x00A81174) before its own. The destructors free the
// containers' storage but never destroy the generator, and the aircraft
// and watercraft managers' deleting destructors do not set the vtables
// back. EAX's top bytes follow the game's where they come from a call or a
// value: QueryInterface (AddRef's answer, or what is left of the IID after
// its compares), Release, IsDrivingMode (the view's ID), CanCreateGroup
// (zero), GetMinZoom (zero), the source building counts. IsActive, IsPaused
// and IsCameraFollowUpdating answer AL alone (the game leaves its caller's
// EAX above it); every caller reads AL.
//
// Still the game's, at their addresses: the adjustor thunks (0x00512750,
// 0x00512760, 0x00512770, 0x004FBC60 to 0x004FBCE0, 0x00574920, 0x00574930,
// 0x00574940; `sub ecx,N; jmp`, which reach our QueryInterface, AddRef and
// Release); the shared stubs 0x004FC900 and 0x005053E0; the containers'
// helpers (the tree erase 0x0050F1E0, the record range 0x00510650, the
// hash table clears 0x008EBA60 and 0x00829D20, the watercraft records'
// range 0x0056F740, the group's class 0x00505330); SC4Random, the hash
// set constructor, the map and hash table finds, SC4PairList::Destroy,
// SC4RefVector::Destroy and the allocators.
#include "core/automata_awmgr.h"

#include "core/address_check.h"
#include "core/fixed_seed.h"
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

namespace opensc4::automata_awmgr {
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
    log::Write("automata awmgr: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

// A list head linked to itself.
char* NewHead(std::uint32_t size)
{
    char* const head = static_cast<char*>(Allocate(size));
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    return head;
}

// A hash table field (0x14 bytes) zeroed but for +0 and made with 100
// buckets, inline in the game.
void ConstructTable(char* table)
{
    At<std::uint32_t>(table, table::kBegin) = 0;
    At<std::uint32_t>(table, table::kEnd) = 0;
    At<std::uint32_t>(table, table::kCapacity) = 0;
    At<std::uint32_t>(table, table::kCount) = 0;
    CallThis<void*>(addr::SC4Uint32HashSet_Construct, table, table::kBuckets);
}

// A tree field {header, size} emptied: the header's root cleared and its
// leftmost and rightmost back to itself; the size zero. Inline in the game.
void ResetTree(char* field)
{
    char* const header = At<char*>(field, 0);
    At<char*>(header, tree::kLeft) = header;
    At<char*>(At<char*>(field, 0), tree::kParent) = nullptr;
    At<char*>(At<char*>(field, 0), tree::kRight) = At<char*>(field, 0);
    At<std::uint32_t>(field, 4) = 0;
}

// ====================================================================================
// The object

// 0x005125E0, the common base's constructor (the three managers' bases call
// it; part v1's 0x00554360 too).
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, field::kVtable4) = addr::SC4Audio_OldTargetBaseVtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4AutomataManager_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4AutomataManager_MessageVtable;
    At<std::uint8_t>(self, field::kByte10) = 0;
    for (std::size_t at = field::kRect; at < field::kInitialized; at += 4)
        At<std::uint32_t>(self, at) = 0;
    At<std::uint8_t>(self, field::kInitialized) = 0;
    At<std::uint32_t>(self, field::kRefCount) = 0;
    At<std::uint32_t>(self, field::kMaxCount) = 0;
    At<std::uint32_t>(self, field::kShare) = 0;
    At<std::uint32_t>(self, field::kAutomata) = 0;
    At<char*>(self, field::kAutomata) = NewHead(0xC);
    At<std::uint32_t>(self, field::kCount) = 0;
    At<std::uint8_t>(self, field::kActive) = 1;
    At<std::uint8_t>(self, field::kPaused) = 0;
    {
        // The clock seed is counted under the original's call site (the
        // return address after its call), not ours.
        const fixed_seed::SiteOverride site(0x00512666);
        CallThis<void*>(addr::SC4Random_ConstructSeeded, self + field::kRandom, static_cast<std::uint32_t>(0xFFFFFFFF));
    }
    At<std::uint32_t>(self, field::kExemplarsBegin) = 0;
    At<std::uint32_t>(self, field::kExemplarsEnd) = 0;
    At<std::uint32_t>(self, field::kExemplarsCapacity) = 0;
    At<std::uint32_t>(self, field::kExemplarMap) = 0;
    char* const header = static_cast<char*>(Allocate(0x20));
    At<char*>(self, field::kExemplarMap) = header;
    At<std::uint32_t>(self, field::kExemplarMapSize) = 0;
    At<std::uint8_t>(header, 0) = 0;
    At<std::uint32_t>(At<char*>(self, field::kExemplarMap), tree::kParent) = 0;
    At<char*>(At<char*>(self, field::kExemplarMap), tree::kLeft) = At<char*>(self, field::kExemplarMap);
    At<char*>(At<char*>(self, field::kExemplarMap), tree::kRight) = At<char*>(self, field::kExemplarMap);
    At<std::uint32_t>(self, field::kA30) = 0;
    At<std::uint32_t>(self, field::kA34) = 0;
    At<std::uint32_t>(self, field::kA38) = 0;
    At<std::uint8_t>(self, field::kMinZoom) = 0;
    At<std::uint8_t>(self, field::kA3D) = 0;
    At<std::uint32_t>(self, field::kA40) = 0;
    At<std::uint32_t>(self, field::kGroup) = 0;
    At<std::uint32_t>(self, field::kAutomatonClass) = 0;
    At<std::uint8_t>(self, field::kA4C) = 0;
    ConstructTable(self + field::kGroupInstances);
    ConstructTable(self + field::kSourceCounts);
    At<std::uint8_t>(self, field::kA78) = 0;
    At<std::uint32_t>(self, field::kCells) = 0;
    At<std::uint32_t>(self, field::kScripts) = 0;
    At<std::uint32_t>(self, field::kControllers) = 0;
    return self;
}

// 0x004FBB40, the aircraft manager's constructor.
char* __fastcall AircraftConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("aircraft Construct", logged);
    CallThis<char*>(addr::cSC4AutomataManager_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AircraftManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4AircraftManager_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AircraftManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4AircraftManager_MessageVtable;
    At<std::uint32_t>(self, field::kAirMap) = 0;
    char* const header = static_cast<char*>(Allocate(0x18));
    At<char*>(self, field::kAirMap) = header;
    At<std::uint32_t>(self, field::kAirMapSize) = 0;
    At<std::uint8_t>(header, 0) = 0;
    At<std::uint32_t>(At<char*>(self, field::kAirMap), tree::kParent) = 0;
    At<char*>(At<char*>(self, field::kAirMap), tree::kLeft) = At<char*>(self, field::kAirMap);
    At<char*>(At<char*>(self, field::kAirMap), tree::kRight) = At<char*>(self, field::kAirMap);
    At<std::uint32_t>(self, field::kAirList) = 0;
    At<char*>(self, field::kAirList) = NewHead(0x18);
    At<std::uint32_t>(self, field::kAirFilter) = 0;
    At<std::uint32_t>(self, field::kA40) = 0x18;
    At<std::uint32_t>(self, field::kGroup) = 0x4003;
    At<std::uint32_t>(self, field::kAutomatonClass) = kAircraftAutomatonClass;
    return self;
}

// 0x00574860, the watercraft manager's constructor.
char* __fastcall WatercraftConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("watercraft Construct", logged);
    CallThis<char*>(addr::cSC4AutomataManager_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4WatercraftManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4WatercraftManager_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4WatercraftManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4WatercraftManager_MessageVtable;
    At<std::uint8_t>(self, field::kWaterByte) = 0;
    ConstructTable(self + field::kWaterTable);
    ConstructTable(self + field::kWaterTable2);
    ConstructTable(self + field::kWaterTable3);
    At<std::uint32_t>(self, field::kWaterRefs) = 0;
    At<std::uint32_t>(self, field::kWaterRefs + 4) = 0;
    At<std::uint32_t>(self, field::kWaterRefs + 8) = 0;
    At<std::uint32_t>(self, field::kA40) = 0x19;
    At<std::uint32_t>(self, field::kGroup) = 0x4412;
    At<std::uint32_t>(self, field::kAutomatonClass) = kWatercraftAutomatonClass;
    return self;
}

// 0x00514600: 0xA9C bytes from SC4::operator_new, the constructor, the
// object + 8 (the serializable part).
void* __cdecl AircraftCreate()
{
    static bool logged;
    FirstCall("aircraft Create", logged);
    char* const self =
        static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(kAircraftSize)));
    if (!self)
        return nullptr;
    char* const made = CallThis<char*>(addr::cSC4AircraftManager_Construct, self);
    if (!made)
        return nullptr;
    return made + field::kSerializable;
}

// 0x00514630: 0xAD4 bytes, the constructor, the object + 8.
void* __cdecl WatercraftCreate()
{
    static bool logged;
    FirstCall("watercraft Create", logged);
    char* const self =
        static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(kWatercraftSize)));
    if (!self)
        return nullptr;
    char* const made = CallThis<char*>(addr::cSC4WatercraftManager_Construct, self);
    if (!made)
        return nullptr;
    return made + field::kSerializable;
}

// 0x00512780, the common base's destructor body: the vtables back to its
// own, the two hash tables cleared and their buckets freed, the exemplar
// map's nodes erased and its header freed, the exemplar records' range
// (0x00510650) and the vector's storage freed, the automata list
// destroyed. Answers what SC4PairList::Destroy leaves in EAX.
std::uint32_t __fastcall Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Destroy", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4AutomataManager_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4AutomataManager_MessageVtable;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + field::kSourceCounts);
    if (void* const buckets = At<void*>(self, field::kSourceCounts + table::kBegin))
        Free(buckets);
    CallThis<void>(addr::SC4HashSetTable_Clear, self + field::kGroupInstances);
    if (void* const buckets = At<void*>(self, field::kGroupInstances + table::kBegin))
        Free(buckets);
    char* const map = self + field::kExemplarMap;
    if (At<std::uint32_t>(self, field::kExemplarMapSize) != 0) {
        CallThis<void>(addr::cSC4AutomataManager_EraseExemplarTree, map,
                       At<void*>(At<char*>(map, 0), tree::kParent));
        ResetTree(map);
    }
    if (void* const header = At<void*>(map, 0))
        Free(header);
    char tag = 0; // the allocator argument, never read
    CallCdecl<void>(addr::cSC4AutomataManager_DestroyExemplars, At<void*>(self, field::kExemplarsBegin),
                    At<void*>(self, field::kExemplarsEnd), static_cast<void*>(&tag));
    if (void* const begin = At<void*>(self, field::kExemplarsBegin))
        Free(begin);
    return CallThis<std::uint32_t>(addr::SC4PairList_Destroy, self + field::kAutomata);
}

// 0x00512CD0, the common base's slot 38 (flags), ret 4.
char* __fastcall DeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    CallThis<std::uint32_t>(addr::cSC4AutomataManager_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// 0x004FB270, on the aircraft map's field (this) a node and its subtree:
// each right subtree erased first, then the node freed and its left taken;
// ret 4. It calls itself at its own address.
void __fastcall AircraftEraseTree(char* field, void*, char* node)
{
    static bool logged;
    FirstCall("aircraft EraseTree", logged);
    while (node) {
        CallThis<void>(addr::cSC4AircraftManager_EraseTree, field, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        Free(node);
        node = left;
    }
}

// 0x004FB780, on the aircraft map's field (+0xA88: header, size): with
// nodes, the tree erased (0x004FB270) and the header reset; then the
// header freed.
void __fastcall AircraftClearTree(char* field, void*)
{
    static bool logged;
    FirstCall("aircraft ClearTree", logged);
    if (At<std::uint32_t>(field, 4) != 0) {
        CallThis<void>(addr::cSC4AircraftManager_EraseTree, field, At<char*>(At<char*>(field, 0), tree::kParent));
        ResetTree(field);
    }
    if (void* const header = At<void*>(field, 0))
        Free(header);
}

// 0x004FBCF0, the aircraft manager's slot 38 (flags), ret 4: the list
// (+0xA94) destroyed, the map cleared (0x004FB780), the common base's
// destructor body.
char* __fastcall AircraftDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("aircraft DeletingDestructor", logged);
    CallThis<std::uint32_t>(addr::SC4PairList_Destroy, self + field::kAirList);
    CallThis<void>(addr::cSC4AircraftManager_ClearTree, self + field::kAirMap);
    CallThis<std::uint32_t>(addr::cSC4AutomataManager_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// 0x0056D670, on a watercraft table of 0xC-byte nodes (+0xAA0, +0xAB4):
// per bucket, each node's references at +8 and +4 released and the node
// freed, the bucket emptied (the bucket count read again each time); the
// count zero.
void __fastcall WatercraftClearPairs(char* t, void*)
{
    static bool logged;
    FirstCall("watercraft ClearPairs", logged);
    std::uint32_t i = 0;
    if (static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)) != 0) {
        do {
            char* node = At<char**>(t, table::kBegin)[i];
            while (node) {
                char* const next = At<char*>(node, 0);
                if (void* const a = At<void*>(node, 8))
                    Virtual<std::uint32_t>(a, slot::kRelease);
                if (void* const b = At<void*>(node, 4))
                    Virtual<std::uint32_t>(b, slot::kRelease);
                Free(node);
                node = next;
            }
            At<char**>(t, table::kBegin)[i] = nullptr;
            ++i;
        } while (i < static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)));
    }
    At<std::uint32_t>(t, table::kCount) = 0;
}

// 0x005743D0, on the watercraft table of 0x20-byte nodes (+0xA8C): per
// bucket, each node's reference at +0x1C released, its record range
// (+0x10, +0x14) destroyed (0x0056F740) and the range's storage freed, the
// references at +0xC, +8 and +4 released and the node freed; the bucket
// emptied; the count zero.
void __fastcall WatercraftClearRoutes(char* t, void*)
{
    static bool logged;
    FirstCall("watercraft ClearRoutes", logged);
    std::uint32_t i = 0;
    if (static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)) != 0) {
        do {
            char* node = At<char**>(t, table::kBegin)[i];
            while (node) {
                char* const next = At<char*>(node, 0);
                if (void* const r = At<void*>(node, 0x1C))
                    Virtual<std::uint32_t>(r, slot::kRelease);
                char tag = 0; // the allocator argument, never read
                CallCdecl<void>(addr::cSC4WatercraftManager_DestroyRecords, At<void*>(node, 0x10),
                                At<void*>(node, 0x14), static_cast<void*>(&tag));
                if (void* const begin = At<void*>(node, 0x10))
                    Free(begin);
                for (const std::size_t at : {std::size_t{0xC}, std::size_t{8}, std::size_t{4}})
                    if (void* const r = At<void*>(node, at))
                        Virtual<std::uint32_t>(r, slot::kRelease);
                Free(node);
                node = next;
            }
            At<char**>(t, table::kBegin)[i] = nullptr;
            ++i;
        } while (i < static_cast<std::uint32_t>(At<char**>(t, table::kEnd) - At<char**>(t, table::kBegin)));
    }
    At<std::uint32_t>(t, table::kCount) = 0;
}

// 0x00574970, the watercraft manager's destructor body: the reference
// vector destroyed, the three tables cleared and their buckets freed, then
// the common base's (a tail jump: its EAX is the answer).
std::uint32_t __fastcall WatercraftDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("watercraft Destroy", logged);
    CallThis<void>(addr::SC4RefVector_Destroy, self + field::kWaterRefs);
    CallThis<void>(addr::cSC4WatercraftManager_ClearPairs, self + field::kWaterTable3);
    if (void* const buckets = At<void*>(self, field::kWaterTable3 + table::kBegin))
        Free(buckets);
    CallThis<void>(addr::cSC4WatercraftManager_ClearPairs, self + field::kWaterTable2);
    if (void* const buckets = At<void*>(self, field::kWaterTable2 + table::kBegin))
        Free(buckets);
    CallThis<void>(addr::cSC4WatercraftManager_ClearRoutes, self + field::kWaterTable);
    if (void* const buckets = At<void*>(self, field::kWaterTable + table::kBegin))
        Free(buckets);
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_Destroy, self);
}

// 0x00574950, the watercraft manager's slot 38 (flags), ret 4.
char* __fastcall WatercraftDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("watercraft DeletingDestructor", logged);
    CallThis<std::uint32_t>(addr::cSC4WatercraftManager_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// AddRef through the object's vtable, AL 1 over its answer.
std::uint32_t Answer(char* object)
{
    char* volatile held = object; // the game calls through it without a test
    return (Virtual<std::uint32_t>(held, slot::kAddRef) & 0xFFFFFF00u) | 1u;
}

// The part at `offset`, or the null branch (a null written, AddRef called
// through the null object as the game does).
std::uint32_t AnswerPart(char* self, std::size_t offset, void** out)
{
    char* volatile object = self;
    if (!object) {
        *out = nullptr;
        return Answer(object);
    }
    *out = self + offset;
    return Answer(self);
}

// 0x00505480, the common base's slot 0 (iid, out), ret 8.
std::uint32_t __fastcall QueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    if (iid > kMessageIid) {
        if (iid == kSerializableIid)
            return AnswerPart(self, field::kSerializable, out);
        return iid & 0xFFFFFF00u;
    }
    if (iid == kMessageIid)
        return AnswerPart(self, field::kMessage, out);
    if (iid == 1)
        return AnswerPart(self, field::kVtable4, out);
    if (iid == kManagerIid) {
        *out = self;
        return Answer(self);
    }
    // What the compares leave: iid - 1 - 0x098F9BF3, AL cleared.
    return (iid - kManagerIid) & 0xFFFFFF00u;
}

std::uint32_t QueryInterfaceOf(char* self, std::uint32_t own, std::uint32_t iid, void** out)
{
    if (iid == own) {
        *out = self;
        return Answer(self);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_QueryInterface, self, iid, out);
}

// 0x004FA740, the aircraft manager's slot 0, ret 8.
std::uint32_t __fastcall AircraftQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("aircraft QueryInterface", logged);
    return QueryInterfaceOf(self, kAircraftClassId, iid, out);
}

// 0x00566E30, the watercraft manager's slot 0, ret 8.
std::uint32_t __fastcall WatercraftQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("watercraft QueryInterface", logged);
    return QueryInterfaceOf(self, kWatercraftClassId, iid, out);
}

// 0x00505510, slot 1 of all four managers: the count at +0x38.
std::uint32_t __fastcall AddRef(char* self, void*)
{
    static bool logged;
    FirstCall("AddRef", logged);
    const std::uint32_t count = At<std::uint32_t>(self, field::kRefCount) + 1;
    At<std::uint32_t>(self, field::kRefCount) = count;
    return count;
}

// 0x00507B50, slot 2 of all four managers: above 1 counted down; else
// Shutdown (slot 4) and the deleting destructor (slot 38, 1), answer 0.
std::uint32_t __fastcall Release(char* self, void*)
{
    static bool logged;
    FirstCall("Release", logged);
    const std::uint32_t count = At<std::uint32_t>(self, field::kRefCount);
    if (count > 1) {
        At<std::uint32_t>(self, field::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<std::uint8_t>(self, slot::kShutdown);
    Virtual<void*>(self, slot::kDeletingDestructor, static_cast<std::uint32_t>(1));
    return 0;
}

// The serializable part's slot 5 (no this read).
std::uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("GetGZCLSID", logged);
    return kBaseClassId;
}

std::uint32_t __fastcall AircraftGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("aircraft GetGZCLSID", logged);
    return kAircraftClassId;
}

std::uint32_t __fastcall WatercraftGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("watercraft GetGZCLSID", logged);
    return kWatercraftClassId;
}

// ====================================================================================
// The queries (slots of all four managers)

// 0x004FBBE0, slot 6 IsActive: +0x4C.
std::uint8_t __fastcall IsActive(char* self, void*)
{
    static bool logged;
    FirstCall("IsActive", logged);
    return At<std::uint8_t>(self, field::kActive);
}

// 0x004FBBF0, slot 9 Pause: +0x4D set.
void __fastcall Pause(char* self, void*)
{
    static bool logged;
    FirstCall("Pause", logged);
    At<std::uint8_t>(self, field::kPaused) = 1;
}

// 0x004FBC00, slot 10 Resume: +0x4D cleared.
void __fastcall Resume(char* self, void*)
{
    static bool logged;
    FirstCall("Resume", logged);
    At<std::uint8_t>(self, field::kPaused) = 0;
}

// 0x004FBC10, slot 11 IsPaused: +0x4D.
std::uint8_t __fastcall IsPaused(char* self, void*)
{
    static bool logged;
    FirstCall("IsPaused", logged);
    return At<std::uint8_t>(self, field::kPaused);
}

// 0x00506940, slot 13 IsDrivingMode: the view service's (0x00B43DC4) slot
// 0x60 object, whose slot 0x14 ID is 0x2B709676 (the U-Drive view). No
// this read.
std::uint32_t __fastcall IsDrivingMode(void*, void*)
{
    static bool logged;
    FirstCall("IsDrivingMode", logged);
    void* const service = Global<void*>(addr::SC4_Service43DC4);
    void* const view = Virtual<void*>(service, 0x60);
    if (!view)
        return 0;
    const std::uint32_t id = Virtual<std::uint32_t>(view, 0x14);
    return id == 0x2B709676u ? (id & 0xFFFFFF00u) | 1u : id & 0xFFFFFF00u;
}

// 0x004FBC20, slot 14 GetMinZoom: +0xA3C, zero-extended.
std::uint32_t __fastcall GetMinZoom(char* self, void*)
{
    static bool logged;
    FirstCall("GetMinZoom", logged);
    return At<std::uint8_t>(self, field::kMinZoom);
}

// 0x005053F0, slot 17, the five-argument Dispatch: 0, ret 0x14.
std::uint32_t __fastcall Dispatch5(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::uint32_t)
{
    static bool logged;
    FirstCall("Dispatch (slot 17)", logged);
    return 0;
}

// 0x004FBC30, slot 28 GetExemplarData: the vector at +0xA18.
char* __fastcall GetExemplarData(char* self, void*)
{
    static bool logged;
    FirstCall("GetExemplarData", logged);
    return self + field::kExemplarsBegin;
}

// 0x004FBC40, slot 29 GetExemplarDataMap: the map at +0xA24.
char* __fastcall GetExemplarDataMap(char* self, void*)
{
    static bool logged;
    FirstCall("GetExemplarDataMap", logged);
    return self + field::kExemplarMap;
}

// 0x0050C300, slot 30 CanCreateGroup (id), ret 4: whether the exemplar map
// holds the ID (SC4Uint32Map::Find against the header). EAX 0 or 1.
std::uint32_t __fastcall CanCreateGroup(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("CanCreateGroup", logged);
    void* node = nullptr;
    CallThis<void>(addr::SC4Uint32Map_Find, self + field::kExemplarMap, &node, &id);
    return node != At<void*>(self, field::kExemplarMap) ? 1u : 0u;
}

// 0x00505400, slot 35 IsCameraFollowUpdating: the byte at 0x00B21B94. No
// this read.
std::uint8_t __fastcall IsCameraFollowUpdating(void*, void*)
{
    static bool logged;
    FirstCall("IsCameraFollowUpdating", logged);
    return Global<std::uint8_t>(addr::SC4AutomataManager_CameraFollowUpdating);
}

// 0x0050BE30, slot 36 GetSourceBuildingCount (id), ret 4: for 0x1402 and
// 0x1406 the slot 0x2C of the object at 0x00B43D28 (0 without one); else
// the count the +0xA64 table keeps for the ID (+8 of its node), 0 when not
// there.
std::uint32_t __fastcall GetSourceBuildingCount(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("GetSourceBuildingCount", logged);
    if (id == 0x1402 || id == 0x1406) {
        void* const source = Global<void*>(addr::SC4_Value43D28);
        if (!source)
            return 0;
        return Virtual<std::uint32_t>(source, 0x2C);
    }
    struct {
        char* node;
        void* table;
    } found = {};
    CallThis<void>(addr::SC4Uint32HashTable_Find, self + field::kSourceCounts, static_cast<void*>(&found), &id);
    if (!found.node)
        return 0;
    return At<std::uint32_t>(found.node, 8);
}

// 0x00505410, slot 37 GetSourceBuildingCountForAutomataGroup (id), ret 4:
// the script system's automata group by ID (slot 6), its class (+0x6C) or
// its parents' (0x00505330), then slot 36 with it (0 for none); 0 without a
// script system or a group.
std::uint32_t __fastcall GetSourceBuildingCountForGroup(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("GetSourceBuildingCountForGroup", logged);
    void* const scripts = At<void*>(self, field::kScripts);
    if (!scripts)
        return 0;
    char* const group = Virtual<char*>(scripts, 0x18, id);
    if (!group)
        return 0;
    if (const std::uint32_t classId = At<std::uint32_t>(group, 0x6C))
        return Virtual<std::uint32_t>(self, slot::kGetSourceBuildingCount, classId);
    if (char* const parent = At<char*>(group, 0xC))
        return Virtual<std::uint32_t>(self, slot::kGetSourceBuildingCount,
                                      CallThis<std::uint32_t>(addr::cSC4AutomataManager_GroupClass, parent));
    return Virtual<std::uint32_t>(self, slot::kGetSourceBuildingCount, static_cast<std::uint32_t>(0));
}

// 0x00507B00, slot 44 (key), ret 4: the first record of the exemplar
// vector whose resource key (+0x10, +0x14, +0x18) is the key's; null when
// none (and for an empty vector, without reading the key).
char* __fastcall FindExemplar(char* self, void*, const std::uint32_t* key)
{
    static bool logged;
    FirstCall("FindExemplar", logged);
    char* record = At<char*>(self, field::kExemplarsBegin);
    char* const end = At<char*>(self, field::kExemplarsEnd);
    if (record == end)
        return nullptr;
    const std::uint32_t type = key[0];
    do {
        if (At<std::uint32_t>(record, exemplar::kKey) == type && At<std::uint32_t>(record, exemplar::kKey + 4) == key[1] &&
            At<std::uint32_t>(record, exemplar::kKey + 8) == key[2])
            return record;
        record += exemplar::kSize;
    } while (record != end);
    return nullptr;
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
            log::Write("automata awmgr %s: no address entry for 0x%08X", what,
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
    log::Write("automata awmgr %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Create, AircraftCreate),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Create, WatercraftCreate),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_Construct, AircraftConstruct),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Construct, WatercraftConstruct),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_EraseTree, AircraftEraseTree),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_ClearTree, AircraftClearTree),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_DeletingDestructor, AircraftDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ClearPairs, WatercraftClearPairs),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_ClearRoutes, WatercraftClearRoutes),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_Destroy, WatercraftDestroy),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_DeletingDestructor, WatercraftDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_QueryInterface, AircraftQueryInterface),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_QueryInterface, WatercraftQueryInterface),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_AddRef, AddRef),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Release, Release),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AircraftManager_GetGZCLSID, AircraftGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4WatercraftManager_GetGZCLSID, WatercraftGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_IsActive, IsActive),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Pause, Pause),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Resume, Resume),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_IsPaused, IsPaused),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_IsDrivingMode, IsDrivingMode),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetMinZoom, GetMinZoom),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_Dispatch5, Dispatch5),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetExemplarData, GetExemplarData),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetExemplarDataMap, GetExemplarDataMap),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_CanCreateGroup, CanCreateGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_IsCameraFollowUpdating, IsCameraFollowUpdating),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetSourceBuildingCount, GetSourceBuildingCount),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_GetSourceBuildingCountForGroup, GetSourceBuildingCountForGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataManager_FindExemplar, FindExemplar),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.awmgr", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AircraftManager_Construct,
                        "Our aircraft and watercraft managers and the base all four automata managers share, the "
                        "object and its queries: the two factories, the constructors (common base, aircraft, "
                        "watercraft), the destructor bodies, the deleting destructors and the container clears "
                        "they call; QueryInterface of the three, the common AddRef and Release, GetGZCLSID of "
                        "the three; IsActive, Pause, Resume, IsPaused, IsDrivingMode, GetMinZoom, the "
                        "five-argument Dispatch stub, GetExemplarData and its map, CanCreateGroup, "
                        "IsCameraFollowUpdating, GetSourceBuildingCount (both forms) and the exemplar find (36 "
                        "detours; part aw1, src/core/automata_awmgr.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_awmgr
