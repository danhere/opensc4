// Our automata script system, part s1 (build 641): the object (the factory,
// the constructor, the destructor body, QueryInterface, Release, the
// serializable part's GetGZCLSID and the +8 part's ID test), the life cycle
// (Init with its default groups, Shutdown with the release of the
// templates and of the groups) and the queries (the map getters of slots
// 5, 8, 9, 10, 15 and 18, the group lookups of slots 6, 7, 11 and 12, the
// drive command (14), the controller template by name (17)), and the
// occupant group template (CreateOccupantGroup, slot 13; the group's
// constructor, destructors, record map clear, Matches (its slot 3), the
// parent chain queries and the name lookup). The class map is in
// src/core/automata_scripts.h and the survey in docs/automata.md, "Part s".
// Calls between functions of the part go to the game's addresses, so each
// detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses, hex,
// eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.scripts.object (7): kObject below.
//   simulators.automata.scripts.life (5): kLife.
//   simulators.automata.scripts.queries (12): kQueries.
//   simulators.automata.scripts.groups (10): kGroups.
//
// What it does. The script system is the city's (city slot 48): Init adds
// its +4 part to the message server (0xC0), makes the two default occupant
// groups (the names at 0x00B09A64 and 0x00B09A68, IDs 0x4002 and 0x4001,
// automata groups of classes cSC4Vehicle and cSC4Pedestrian), loads the
// automata scripts (0x00523B10, the game's) and registers the Lua table
// `automata` with seven functions (create_automaton, create_controller,
// attach_controller, attach_controller_group, detach_controller,
// detach_controller_group, get_source_building_count; the game's).
// Shutdown undoes it and releases what the scripts made: the controller
// templates (deleting destructor), the groups of the three group maps and
// the special filter list (Release). The scripts make the groups through
// CreateOccupantGroup: a new group template is put into the name map (a
// name already there is refused: the message is formatted and dropped, the
// group destroyed), with the automata flag also into the automata group
// map, then into the ID map or, with ID 0, onto the special filter list,
// one reference for each.
//
// Oddities kept: QueryInterface's null branches write a null and then call
// AddRef through the object (the address is held in a volatile local, as
// in parts a1 and a3a); Release at a count of 1 (or 0) deletes without counting
// down; CreateOccupantGroup writes the group ID without a null test, ignores the automata map's insert answer, and formats
// its refusal into a string it frees; AddDefaultGroups writes the class
// into the group it gets without a test; Matches seeds a cRZRandom on its
// stack for one draw and never destroys it, and draws only for a
// percentage below 1.0.
//
// Floats: Matches compares the percentage (a float) with 1.0 and with the
// draw cRZRandom::NextFloat answers in ST0 (a double here, unrounded), with
// the asm's handling of unordered values (a NaN percentage matches without
// a draw; a NaN draw does not match).
//
// Still the game's, at their addresses: AddRef 0x00695330 (shared); the
// adjustor thunks 0x005200C0, 0x005200D0, 0x005200E0, 0x005200F0,
// 0x00520100, 0x00520110, 0x00521670, 0x00521680 and 0x00521690 (`sub
// ecx,N; jmp`, which reach our QueryInterface and Release); the group
// template's base (0x005EA080, 0x005E95C0, its vtable 0x00A96BF4's slots),
// its AddRef and Release (0x004BDF00, 0x007CB920) and slots 4 and 5
// (0x004FC900, shared); the hash map helpers (find 0x0051C9F0, insert
// 0x0051FDC0, begin, clear, grow) and the allocators; the scripts' loading
// (0x00523B10 and what it calls, among them 0x0051C4C0, which holds the
// Lua print patch site 0x0051C50B) and the Lua callbacks (parts s2, s3).
#include "core/automata_scripts.h"

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

namespace opensc4::automata_scripts {
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
    log::Write("automata scripts: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

// The game's SC4String on the stack (vtable 0x00A80810, the STL-style
// begin, end and capacity, a fifth word the functions here zero).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

// A string hash map's insert pair: the key (begin, end, capacity) and the
// value, as the node holds them at +4.
struct NamePair {
    char* begin;
    char* end;
    char* capacity;
    void* value;
};

// An insert's answer: the iterator (node, table) and whether it was put in.
struct InsertResult {
    void* node;
    void* table;
    std::uint8_t inserted;
    std::uint8_t pad[3];
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

// The first node of the first bucket that has one (the inline begin of
// 0x005201D0), or null.
char* FirstNode(char* table)
{
    for (std::uint32_t i = 0; i < BucketCount(table); ++i)
        if (char* const node = Bucket(table, i))
            return node;
    return nullptr;
}

// The node after `node` in the bucket order, as the iterator's increment
// inlined in 0x00520120 and 0x005201D0 finds it: its chain's next, else the
// first node of a later bucket, starting after its key's bucket.
char* NextAfterBucket(char* table, std::uint32_t bucket)
{
    for (std::uint32_t i = bucket + 1; i < BucketCount(table); ++i)
        if (char* const node = Bucket(table, i))
            return node;
    return nullptr;
}

std::uint32_t StringHash(const char* begin, const char* end)
{
    std::uint32_t h = 0;
    const std::uint32_t length = static_cast<std::uint32_t>(end - begin);
    for (std::uint32_t i = 0; i < length; ++i)
        h = h * 5 + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(begin[i])));
    return h;
}

char* NextStringNode(char* node, char* table)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    const std::uint32_t h = StringHash(At<char*>(node, 4), At<char*>(node, 8));
    return NextAfterBucket(table, h % BucketCount(table));
}

char* NextUint32Node(char* node, char* table)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    return NextAfterBucket(table, At<std::uint32_t>(node, 4) % BucketCount(table));
}

// The key a string hash map is searched with: the SC4String's STL part
// (+4), or null for a null string.
const void* KeyOf(const void* name) { return name ? static_cast<const char*>(name) + 4 : nullptr; }

// A uint32 hash map's lookup, inline in slots 11 and 14.
bool FindUint32(char* table, std::uint32_t key, std::uint32_t& value)
{
    for (char* node = Bucket(table, key % BucketCount(table)); node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, 4) == key) {
            value = At<std::uint32_t>(node, 8);
            return true;
        }
    return false;
}

// Puts (name, value) into a string hash map as CreateOccupantGroup does:
// the key copied into a pair on the stack, the buckets grown for one more,
// the insert, the pair's copy freed. Answers whether it went in.
bool InsertByName(char* self, std::size_t mapField, const char* name, void* value)
{
    const char* const key = static_cast<const char*>(KeyOf(name));
    NamePair pair{nullptr, nullptr, nullptr, nullptr};
    std::uint8_t allocator = 0;
    CallThis<void>(addr::cSC4Advice_F408420, &pair, At<const char*>(const_cast<char*>(key), 0),
                   At<const char*>(const_cast<char*>(key), 4), static_cast<void*>(&allocator));
    pair.value = value;
    char* const table = self + mapField;
    CallThis<void>(addr::SC4StringHash_Grow, table, At<std::uint32_t>(table, map::kCount) + 1);
    InsertResult result{};
    CallThis<void*>(addr::SC4StringHashMap_InsertUnique, table, static_cast<void*>(&result),
                    static_cast<const void*>(&pair));
    if (pair.begin)
        Free(pair.begin);
    return result.inserted != 0;
}

// ====================================================================================
// The object

// 0x005148A0, the factory (cdecl): a 0xB8-byte block and the constructor.
void* __cdecl Create()
{
    static bool logged;
    FirstCall("Create", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(kObjectSize));
    if (!block)
        return nullptr;
    return CallThis<void*>(addr::cSC4AutomataScriptSystem_Construct, block);
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

char* NewListHead(std::uint32_t nodeSize)
{
    char* const head = static_cast<char*>(Allocate(nodeSize));
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    return head;
}

// 0x005207A0, the constructor.
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, field::kVtable8) = addr::SC4Audio_OldTargetBaseVtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataScriptSystem_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4AutomataScriptSystem_Vtable4;
    At<std::uintptr_t>(self, field::kVtable8) = addr::cSC4AutomataScriptSystem_Vtable8;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataScriptSystem_SerializableVtable;
    At<std::uint8_t>(self, field::kInitialized) = 0;
    At<std::uint32_t>(self, field::kRefCount) = 0;
    At<std::uint32_t>(self, field::kObject18) = 0;
    CallThis<void*>(addr::SC4ScriptSource_Construct, self + field::kSource);
    At<std::uint8_t>(self, field::kLoadByte) = 0;
    ConstructMap(self, field::kGroupsByName);
    ConstructMap(self, field::kAutomataGroups);
    ConstructMap(self, field::kGroupsById);
    At<std::uint32_t>(self, field::kSpecialGroups) = 0;
    At<char*>(self, field::kSpecialGroups) = NewListHead(0xC);
    ConstructMap(self, field::kTemplates);
    At<std::uint32_t>(self, field::kTrackGroups) = 0;
    At<char*>(self, field::kTrackGroups) = NewListHead(0xC);
    ConstructMap(self, field::kDriveCommands);
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

// 0x0051FF70, the destructor body (Release calls it).
void __fastcall Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Destroy", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataScriptSystem_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4AutomataScriptSystem_Vtable4;
    At<std::uintptr_t>(self, field::kVtable8) = addr::cSC4AutomataScriptSystem_Vtable8;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataScriptSystem_SerializableVtable;
    DestroyMap(self, field::kDriveCommands, addr::SC4Uint32HashSet_Clear);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kTrackGroups);
    DestroyMap(self, field::kTemplates, addr::SC4HashTable_Clear);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kSpecialGroups);
    DestroyMap(self, field::kGroupsById, addr::SC4Uint32HashSet_Clear);
    DestroyMap(self, field::kAutomataGroups, addr::SC4HashTable_Clear);
    DestroyMap(self, field::kGroupsByName, addr::SC4HashTable_Clear);
    CallThis<void>(addr::SC4ScriptSource_Destroy, self + field::kSource);
    if (void* const object = At<void*>(self, field::kObject18))
        Virtual<std::uint32_t>(object, slot::kRelease);
}

// 0x0051C2E0, slot 0 (iid, out), ret 8. The +4, +8 and +0xC parts reach it
// through the adjustor thunks 0x005200C0, 0x005200E0 and 0x00520100.
std::uint8_t __fastcall QueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    char* volatile object = self; // the game calls through it without a test
    std::size_t part;
    if (iid == kScriptSystemIid) {
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

// 0x00521320, slot 2: at a count above 1 counts down and answers it; else
// Shutdown (slot 4), the destructor body and the block freed, answer 0.
std::uint32_t __fastcall Release(char* self, void*)
{
    static bool logged;
    FirstCall("Release", logged);
    const std::uint32_t count = At<std::uint32_t>(self, field::kRefCount);
    if (count > 1) {
        At<std::uint32_t>(self, field::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<std::uint32_t>(self, slot::kShutdown);
    CallThis<void>(addr::cSC4AutomataScriptSystem_Destroy, self);
    CallCdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// 0x00520050, the serializable part's GetGZCLSID.
std::uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// 0x0051C4B0, the +8 part's slot 3 (this = object + 8), ret 4: whether the
// ID it points to is 0x87163754.
std::uint8_t __fastcall TestId8(void*, void*, const std::uint32_t* id)
{
    static bool logged;
    FirstCall("TestId8", logged);
    return *id == 0x87163754u ? 1 : 0;
}

// ====================================================================================
// The life cycle

// The interface Init and Shutdown hand to the script registrations: the
// framework's (0xEBE0E860, 0xE898ED03) into a holder, or none.
void* ScriptHolder(void*& holder)
{
    holder = nullptr;
    if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
        Virtual<std::uint32_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, static_cast<void**>(&holder));
    return holder;
}

// 0x0051D9B0, Init's default groups: for the names at 0x00B09A64 and
// 0x00B09A68, CreateOccupantGroup (slot 13) with IDs 0x4002 and 0x4001 as
// automata groups, and their class (+0x14) cSC4Vehicle and cSC4Pedestrian.
void __fastcall AddDefaultGroups(char* self, void*)
{
    static bool logged;
    FirstCall("AddDefaultGroups", logged);
    struct Default {
        std::uintptr_t name;
        std::uint32_t id;
        std::uint32_t automatonClass;
    };
    static constexpr Default kDefaults[] = {
        {addr::cSC4AutomataScriptSystem_VehicleGroupName, 0x4002u, 0x896E714Au},
        {addr::cSC4AutomataScriptSystem_PedestrianGroupName, 0x4001u, 0x896E75AFu},
    };
    for (const Default& d : kDefaults) {
        const char* const text = Global<const char*>(d.name);
        GameString name{0, nullptr, nullptr, nullptr, 0};
        std::uint8_t allocator = 0;
        CallThis<void>(addr::SC4String_FromRange, &name.begin, text, text + std::strlen(text),
                       static_cast<void*>(&allocator));
        name.vtable = addr::SC4String_Vtable;
        name.extra = 0;
        char* const made = Virtual<char*>(self, slot::kCreateOccupantGroup, static_cast<void*>(&name), d.id,
                                          static_cast<std::uint32_t>(1));
        if (name.begin)
            Free(name.begin);
        At<std::uint32_t>(made, group::kClass) = d.automatonClass;
    }
}

// 0x00523C80, slot 3: once (+0x10), the +4 part added to the message server
// (slot 0x14 with 0xC0), the default groups, +0x48 cleared, the scripts
// loaded (0x00523B10), the table `automata` and its seven functions
// registered; AL 1.
std::uint8_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    if (void* const messages = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<std::uint32_t>(messages, 0x14, static_cast<void*>(self + field::kVtable4), 0xC0u);
    CallThis<void>(addr::cSC4AutomataScriptSystem_AddDefaultGroups, self);
    At<std::uint8_t>(self, field::kLoadByte) = 0;
    CallThis<void>(addr::cSC4AutomataScriptSystem_LoadScripts, self);
    void* holder = nullptr;
    CallCdecl<void>(addr::SC4Script_RegisterTable, static_cast<void*>(self), addr::cSC4AutomataScriptSystem_TableName,
                    0u, 0u, 0u, ScriptHolder(holder));
    CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    struct Function {
        std::uintptr_t callback;
        std::uintptr_t name;
    };
    static constexpr Function kFunctions[] = {
        {addr::cSC4AutomataScriptSystem_LuaCreateAutomaton, addr::cSC4AutomataScriptSystem_CreateAutomatonName},
        {addr::cSC4AutomataScriptSystem_LuaCreateController, addr::cSC4AutomataScriptSystem_CreateControllerName},
        {addr::cSC4AutomataScriptSystem_LuaAttachController, addr::cSC4AutomataScriptSystem_AttachControllerName},
        {addr::cSC4AutomataScriptSystem_LuaAttachControllerGroup,
         addr::cSC4AutomataScriptSystem_AttachControllerGroupName},
        {addr::cSC4AutomataScriptSystem_LuaDetachController, addr::cSC4AutomataScriptSystem_DetachControllerName},
        {addr::cSC4AutomataScriptSystem_LuaDetachControllerGroup,
         addr::cSC4AutomataScriptSystem_DetachControllerGroupName},
        {addr::cSC4AutomataScriptSystem_LuaGetSourceBuildingCount,
         addr::cSC4AutomataScriptSystem_GetSourceBuildingCountName},
    };
    for (const Function& f : kFunctions) {
        CallCdecl<void>(addr::SC4Script_RegisterFunction, f.callback, f.name, f.callback, static_cast<void*>(self), 0u,
                        static_cast<void*>(self), ScriptHolder(holder));
        CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    }
    At<std::uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// 0x00520120, Shutdown's release of the controller templates (+0x8C): each
// template's slot 0 (the deleting destructor) with 1, then the map cleared.
void __fastcall ReleaseTemplates(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseTemplates", logged);
    char* const templates = self + field::kTemplates;
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, templates, static_cast<void*>(&it));
    for (char* node = it.node; node; node = NextStringNode(node, it.table))
        if (void* const made = At<void*>(node, 0x10))
            Virtual<void*>(made, 0x00, static_cast<std::uint32_t>(1));
    CallThis<void>(addr::SC4HashTable_Clear, templates);
}

// 0x005201D0, Shutdown's release of the groups: the values of the name
// map (+0x4C) and the automata group map (+0x60) released and each map
// cleared, those of the ID map (+0x74) and the special filter list (+0x88)
// too, the list's nodes freed and its head left empty.
void __fastcall ReleaseGroups(char* self, void*)
{
    static bool logged;
    FirstCall("ReleaseGroups", logged);
    char* const byName = self + field::kGroupsByName;
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, byName, static_cast<void*>(&it));
    for (char* node = it.node; node; node = NextStringNode(node, it.table))
        if (void* const made = At<void*>(node, 0x10))
            Virtual<std::uint32_t>(made, slot::kRelease);
    CallThis<void>(addr::SC4HashTable_Clear, byName);
    char* const automata = self + field::kAutomataGroups;
    for (char* node = FirstNode(automata); node; node = NextStringNode(node, automata))
        if (void* const made = At<void*>(node, 0x10))
            Virtual<std::uint32_t>(made, slot::kRelease);
    CallThis<void>(addr::SC4HashTable_Clear, automata);
    char* const byId = self + field::kGroupsById;
    for (char* node = FirstNode(byId); node; node = NextUint32Node(node, byId))
        if (void* const made = At<void*>(node, 8))
            Virtual<std::uint32_t>(made, slot::kRelease);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, byId);
    for (char* node = At<char*>(At<char*>(self, field::kSpecialGroups), 0); node != At<char*>(self, field::kSpecialGroups);
         node = At<char*>(node, 0))
        if (void* const made = At<void*>(node, 8))
            Virtual<std::uint32_t>(made, slot::kRelease);
    char* node = At<char*>(At<char*>(self, field::kSpecialGroups), 0);
    while (node != At<char*>(self, field::kSpecialGroups)) {
        char* const next = At<char*>(node, 0);
        Free(node);
        node = next;
    }
    char* const head = At<char*>(self, field::kSpecialGroups);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
}

// 0x005208A0, slot 4: when initialized, the table `automata` unregistered,
// the +4 part taken off the message server (slot 0x18 with 0xC0), +0x18
// shut down (its slot 4) and released, the templates and the groups
// released, +0x10 cleared; AL 1.
std::uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("Shutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    void* holder = nullptr;
    CallCdecl<void>(addr::SC4Script_UnregisterTable, static_cast<void*>(self), ScriptHolder(holder));
    CallThis<void>(addr::SC4_ReleaseRefHolder, static_cast<void*>(&holder));
    if (void* const messages = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<std::uint32_t>(messages, 0x18, static_cast<void*>(self + field::kVtable4), 0xC0u);
    if (void* const object = At<void*>(self, field::kObject18)) {
        Virtual<std::uint32_t>(object, slot::kShutdown);
        if (void* const again = At<void*>(self, field::kObject18)) {
            At<void*>(self, field::kObject18) = nullptr;
            Virtual<std::uint32_t>(again, slot::kRelease);
        }
    }
    CallThis<void>(addr::cSC4AutomataScriptSystem_ReleaseTemplates, self);
    CallThis<void>(addr::cSC4AutomataScriptSystem_ReleaseGroups, self);
    At<std::uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// ====================================================================================
// The queries

// 0x00520060, slot 5: the automata group map (+0x60).
void* __fastcall GetAutomataGroupMap(char* self, void*)
{
    static bool logged;
    FirstCall("GetAutomataGroupMap", logged);
    return self + field::kAutomataGroups;
}

// 0x0051C2D0, slot 6 (GetAutomataGroup by ID), ret 4: a jump to slot 11.
void* __fastcall GetAutomataGroupById(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("GetAutomataGroupById", logged);
    return Virtual<void*>(self, slot::kGetOccupantGroupById, id);
}

// A string hash map's value for a name (SC4StringHashSet::Find on the
// name's STL part), or null.
void* FindByName(char* self, std::size_t mapField, const void* name)
{
    char* const node = CallThis<char*>(addr::SC4StringHashSet_Find, self + mapField, KeyOf(name));
    return node ? At<void*>(node, 0x10) : nullptr;
}

// 0x0051CF60, slot 7 (GetAutomataGroup by name), ret 4: the automata group
// map's (+0x60).
void* __fastcall GetAutomataGroupByName(char* self, void*, const void* name)
{
    static bool logged;
    FirstCall("GetAutomataGroupByName", logged);
    return FindByName(self, field::kAutomataGroups, name);
}

// 0x00520080, slot 8: the name map (+0x4C).
void* __fastcall GetOccupantGroupNameMap(char* self, void*)
{
    static bool logged;
    FirstCall("GetOccupantGroupNameMap", logged);
    return self + field::kGroupsByName;
}

// 0x00520090, slot 9: the ID map (+0x74).
void* __fastcall GetOccupantGroupIDMap(char* self, void*)
{
    static bool logged;
    FirstCall("GetOccupantGroupIDMap", logged);
    return self + field::kGroupsById;
}

// 0x005200A0, slot 10: the special filter list's field (+0x88).
void* __fastcall GetSpecialFilterOccupantGroups(char* self, void*)
{
    static bool logged;
    FirstCall("GetSpecialFilterOccupantGroups", logged);
    return self + field::kSpecialGroups;
}

// 0x0051CFF0, slot 11 (GetOccupantGroup by ID), ret 4: the ID map's (+0x74).
std::uint32_t __fastcall GetOccupantGroupById(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("GetOccupantGroupById", logged);
    std::uint32_t value = 0;
    return FindUint32(self + field::kGroupsById, id, value) ? value : 0;
}

// 0x0051CFC0, slot 12 (GetOccupantGroup by name), ret 4: the name map's.
void* __fastcall GetOccupantGroupByName(char* self, void*, const void* name)
{
    static bool logged;
    FirstCall("GetOccupantGroupByName", logged);
    return FindByName(self, field::kGroupsByName, name);
}

// 0x0051D1D0, slot 14, ret 4: the drive command map's (+0xA4), 0xD when
// the key is not there.
std::uint32_t __fastcall GetDriveCommand(char* self, void*, std::uint32_t key)
{
    static bool logged;
    FirstCall("GetDriveCommand", logged);
    std::uint32_t value = 0;
    return FindUint32(self + field::kDriveCommands, key, value) ? value : 0xDu;
}

// 0x00520070, slot 15: the controller template map (+0x8C).
void* __fastcall GetControllerTemplateMap(char* self, void*)
{
    static bool logged;
    FirstCall("GetControllerTemplateMap", logged);
    return self + field::kTemplates;
}

// 0x0051CF90, slot 17, ret 4: the controller template of a name.
void* __fastcall GetControllerTemplate(char* self, void*, const void* name)
{
    static bool logged;
    FirstCall("GetControllerTemplate", logged);
    return FindByName(self, field::kTemplates, name);
}

// 0x005200B0, slot 18: the track building groups' field (+0xA0).
void* __fastcall GetTrackBuildingGroups(char* self, void*)
{
    static bool logged;
    FirstCall("GetTrackBuildingGroups", logged);
    return self + field::kTrackGroups;
}

// ====================================================================================
// The occupant group template

// 0x00520BD0, slot 13 (name, ID, automata flag), ret 0xC: see the top of
// the file.
char* __fastcall CreateOccupantGroup(char* self, void*, const void* name, std::uint32_t id, std::uint8_t automata)
{
    static bool logged;
    FirstCall("CreateOccupantGroup", logged);
    void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(kGroupSize));
    char* const made = block ? CallThis<char*>(addr::cSC4OccupantGroupTemplate_Construct, block) : nullptr;
    At<std::uint32_t>(made, group::kId) = id;
    if (!InsertByName(self, field::kGroupsByName, static_cast<const char*>(name), made)) {
        GameString message{0, nullptr, nullptr, nullptr, 0};
        message.begin = static_cast<char*>(Allocate(8));
        message.capacity = message.begin + 8;
        message.end = message.begin;
        *message.begin = 0;
        message.vtable = addr::SC4String_Vtable;
        message.extra = 0;
        const char* const text = Virtual<const char*>(const_cast<void*>(name), 0x14);
        CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(&message),
                        addr::cSC4AutomataScriptSystem_GroupExistsFormat, text);
        Virtual<void*>(made, slot::kGroupDeletingDestructor, static_cast<std::uint32_t>(1));
        if (message.begin)
            Free(message.begin);
        return nullptr;
    }
    Virtual<std::uint32_t>(made, slot::kAddRef);
    if (automata) {
        InsertByName(self, field::kAutomataGroups, static_cast<const char*>(name), made);
        Virtual<std::uint32_t>(made, slot::kAddRef);
    }
    if (id) {
        struct {
            std::uint32_t key;
            void* value;
        } pair{id, made};
        char* const table = self + field::kGroupsById;
        CallThis<void>(addr::SC4Uint32HashSet_Resize, table, At<std::uint32_t>(table, map::kCount) + 1);
        InsertResult result{};
        CallThis<void*>(addr::SC4Uint32HashMap_Insert, table, static_cast<void*>(&result),
                        static_cast<const void*>(&pair));
        Virtual<std::uint32_t>(made, slot::kAddRef);
        return made;
    }
    char* const head = At<char*>(self, field::kSpecialGroups);
    char* const node = static_cast<char*>(Allocate(0xC));
    if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0)
        At<char*>(node, 8) = made;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 4) = last;
    At<char*>(node, 0) = head;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
    Virtual<std::uint32_t>(made, slot::kAddRef);
    return made;
}

// 0x005206B0, the group template's constructor.
char* __fastcall GroupConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("GroupConstruct", logged);
    CallThis<void*>(addr::SC4_OccupantFilterBase, self);
    At<std::uintptr_t>(self, group::kVtable) = addr::cSC4OccupantGroupTemplate_Vtable;
    At<std::uintptr_t>(self, group::kVtable4) = addr::cSC4OccupantGroupTemplate_Vtable4;
    CallThis<void*>(addr::SC4ScriptNumber_Construct, self + group::kNumber);
    At<std::uint32_t>(self, group::kList30) = 0;
    At<char*>(self, group::kList30) = NewListHead(0xC);
    ConstructMap(self, group::kNames);
    At<std::uint32_t>(self, group::kList48) = 0;
    At<char*>(self, group::kList48) = NewListHead(0x14);
    At<std::uint32_t>(self, group::kList50) = 0;
    At<char*>(self, group::kList50) = NewListHead(0xC);
    At<std::uint32_t>(self, group::kList54) = 0;
    At<char*>(self, group::kList54) = NewListHead(0x1C);
    ConstructMap(self, group::kRecords);
    At<std::uint32_t>(self, group::kParent) = 0;
    At<std::uint32_t>(self, group::kId) = 0;
    At<std::uint32_t>(self, group::kClass) = 0;
    At<std::uint32_t>(self, group::kObject60) = 0;
    At<std::uint32_t>(self, group::kObject64) = 0;
    At<std::uint32_t>(self, group::kZero68) = 0;
    At<std::uint32_t>(self, group::kZero6C) = 0;
    At<std::uint16_t>(self, group::kWord4C) = 0xFFFEu;
    At<std::uint16_t>(self, group::kWord4E) = 0xFFFEu;
    At<std::uint32_t>(self, group::kPercentage) = 0x3F800000u; // 1.0f
    return self;
}

// 0x0051FC80, the group template's record map clear (this = the map at
// +0x70): per bucket, each node's three SC4Strings' buffers (+0x34, +0x20,
// +0xC) freed with their vtables reset, the node freed, the bucket
// emptied; the count zeroed.
void __fastcall GroupClearRecords(char* table, void*)
{
    static bool logged;
    FirstCall("GroupClearRecords", logged);
    for (std::uint32_t i = 0; i < BucketCount(table); ++i) {
        char* node = Bucket(table, i);
        while (node) {
            char* const next = At<char*>(node, 0);
            static constexpr std::size_t kStrings[] = {0x30, 0x1C, 0x08};
            for (const std::size_t at : kStrings) {
                At<std::uintptr_t>(node, at) = addr::SC4String_Vtable;
                if (void* const buffer = At<void*>(node, at + 4))
                    Free(buffer);
            }
            Free(node);
            node = next;
        }
        At<char**>(table, map::kBegin)[i] = nullptr;
    }
    At<std::uint32_t>(table, map::kCount) = 0;
}

// 0x00520410, the group template's destructor body.
void __fastcall GroupDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("GroupDestroy", logged);
    At<std::uintptr_t>(self, group::kVtable) = addr::cSC4OccupantGroupTemplate_Vtable;
    At<std::uintptr_t>(self, group::kVtable4) = addr::cSC4OccupantGroupTemplate_Vtable4;
    if (void* const object = At<void*>(self, group::kObject64)) {
        CallCdecl<void>(addr::SC4_operator_delete, object);
        At<void*>(self, group::kObject64) = nullptr;
    }
    if (void* const object = At<void*>(self, group::kObject60)) {
        CallCdecl<void>(addr::SC4_operator_delete, object);
        At<void*>(self, group::kObject60) = nullptr;
    }
    CallThis<void>(addr::cSC4OccupantGroupTemplate_ClearRecords, self + group::kRecords);
    if (void* const buckets = At<void*>(self + group::kRecords, map::kBegin))
        Free(buckets);
    CallThis<void>(addr::SC4UI_F43ABA0, self + group::kList54);
    if (void* const head = At<void*>(self, group::kList54))
        Free(head);
    CallThis<void>(addr::SC4PairList_Destroy, self + group::kList50);
    CallThis<void>(addr::SC4PairList_Destroy, self + group::kList48);
    CallThis<void>(addr::SC4HashTable_Clear, self + group::kNames);
    if (void* const buckets = At<void*>(self + group::kNames, map::kBegin))
        Free(buckets);
    CallThis<void>(addr::SC4PairList_Destroy, self + group::kList30);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, self + group::kNumber);
    CallThis<void>(addr::SC4OccupantFilterBase_Destroy, self);
}

// 0x00520780, the group template's slot 6 (flags), ret 4.
char* __fastcall GroupDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("GroupDeletingDestructor", logged);
    CallThis<void>(addr::cSC4OccupantGroupTemplate_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// 0x0051C3D0, the group template's slot 3 (occupant), ret 4: whether the
// occupant's slot 0x18 part's 0xAA1DD396 list (its slot 0x14, then 0x19C
// the IDs and 0x10 their count) holds the group ID; then, for a
// percentage below 1.0, whether a draw (a clock-seeded cRZRandom on the
// stack) is not above it.
std::uint8_t __fastcall GroupMatches(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("GroupMatches", logged);
    std::uint8_t found = 0;
    void* const part = Virtual<void*>(occupant, 0x18);
    void* const list = Virtual<void*>(part, 0x24, 0xAA1DD396u);
    if (At<std::uint32_t>(self, group::kId) && list) {
        void* const ids = Virtual<void*>(list, 0x14);
        const std::uint32_t* const values = Virtual<const std::uint32_t*>(ids, 0x19C);
        std::uint32_t i = 0;
        if (Virtual<std::uint32_t>(ids, 0x10) > 0) {
            for (;;) {
                if (values[i] == At<std::uint32_t>(self, group::kId)) {
                    found = 1;
                    break;
                }
                ++i;
                if (!(i < Virtual<std::uint32_t>(ids, 0x10)))
                    break;
            }
        }
    }
    if (!found)
        return 0;
    const double percentage = At<float>(self, group::kPercentage);
    const double one = Global<float>(addr::SC4_FloatOneA81228);
    if (!(percentage < one)) // not below, or unordered: no draw
        return found;
    alignas(4) char random[0x18];
    {
        // The clock seed is counted under the original's call site (the
        // return address after its cRZRandom call), not ours.
        const fixed_seed::SiteOverride site(0x0051C461);
        CallThis<void>(addr::SC4Random_Construct, random, static_cast<std::uint32_t>(0xFFFFFFFF));
    }
    const double draw = CallThis<long double>(addr::cRZRandom_NextFloat, random);
    return percentage < draw || draw != draw ? 0 : 1;
}

// 0x0051C380, ret 4: whether the group or a parent (+0xC) has the ID.
std::uint8_t __fastcall GroupIsInGroup(char* self, void*, std::uint32_t id)
{
    static bool logged;
    FirstCall("GroupIsInGroup", logged);
    // The first compare reads this without a test, as the asm does.
    for (char* g = self;;) {
        if (At<std::uint32_t>(g, group::kId) == id)
            return 1;
        g = At<char*>(g, group::kParent);
        if (!g)
            return 0;
    }
}

// 0x0051C3B0: the first class (+0x14) up the parent chain, the last
// group's when none is set.
std::uint32_t __fastcall GroupGetClass(char* self, void*)
{
    static bool logged;
    FirstCall("GroupGetClass", logged);
    char* g = self;
    while (!At<std::uint32_t>(g, group::kClass) && At<char*>(g, group::kParent))
        g = At<char*>(g, group::kParent);
    return At<std::uint32_t>(g, group::kClass);
}

// 0x0051C4A0: the groups on the parent chain, this one included (0 for
// null).
std::uint32_t __fastcall GroupDepth(char* self, void*)
{
    static bool logged;
    FirstCall("GroupDepth", logged);
    std::uint32_t count = 0;
    for (char* g = self; g; g = At<char*>(g, group::kParent))
        ++count;
    return count;
}

// 0x0051D970 (name, out), ret 8: the value of the group's name map (+0x34)
// for the name into *out, AL 1; AL 0 when it is not there.
std::uint8_t __fastcall GroupFindName(char* self, void*, const void* name, std::uint32_t* out)
{
    static bool logged;
    FirstCall("GroupFindName", logged);
    char* const node = CallThis<char*>(addr::SC4StringHashSet_Find, self + group::kNames, KeyOf(name));
    if (!node)
        return 0;
    *out = At<std::uint32_t>(node, 0x10);
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
            log::Write("automata scripts %s: no address entry for 0x%08X", what,
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
    log::Write("automata scripts %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_Create, Create),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_Release, Release),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_TestId8, TestId8),
};

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_Init, Init),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_AddDefaultGroups, AddDefaultGroups),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReleaseTemplates, ReleaseTemplates),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReleaseGroups, ReleaseGroups),
};

const Detour kQueries[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetAutomataGroupMap, GetAutomataGroupMap),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetAutomataGroupById, GetAutomataGroupById),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetAutomataGroupByName, GetAutomataGroupByName),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetOccupantGroupNameMap, GetOccupantGroupNameMap),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetOccupantGroupIDMap, GetOccupantGroupIDMap),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetSpecialFilterOccupantGroups, GetSpecialFilterOccupantGroups),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetOccupantGroupById, GetOccupantGroupById),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetOccupantGroupByName, GetOccupantGroupByName),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetDriveCommand, GetDriveCommand),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetControllerTemplateMap, GetControllerTemplateMap),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetControllerTemplate, GetControllerTemplate),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_GetTrackBuildingGroups, GetTrackBuildingGroups),
};

const Detour kGroups[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_CreateOccupantGroup, CreateOccupantGroup),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Construct, GroupConstruct),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Destroy, GroupDestroy),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_DeletingDestructor, GroupDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_ClearRecords, GroupClearRecords),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Matches, GroupMatches),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_IsInGroup, GroupIsInGroup),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_GetClass, GroupGetClass),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Depth, GroupDepth),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_FindName, GroupFindName),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode) { return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object"); }
bool InstallLife(registry::Mode) { return InstallList(kLife, sizeof kLife / sizeof kLife[0], "life"); }
bool InstallQueries(registry::Mode) { return InstallList(kQueries, sizeof kQueries / sizeof kQueries[0], "queries"); }
bool InstallGroups(registry::Mode) { return InstallList(kGroups, sizeof kGroups / sizeof kGroups[0], "groups"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.scripts.object", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_Construct,
                        "Our automata script system, the object: the factory, the constructor, the destructor body, "
                        "QueryInterface, Release, the serializable part's GetGZCLSID and the +8 part's ID test (7 "
                        "detours; part s1, src/core/automata_scripts.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.automata.scripts.life", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_Init,
                        "Our automata script system's life cycle: Init with its default groups, Shutdown with the "
                        "release of the controller templates and of the groups (5 detours; part s1, "
                        "src/core/automata_scripts.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.automata.scripts.queries", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_GetOccupantGroupById,
                        "Our automata script system's queries: the map getters (slots 5, 8, 9, 10, 15, 18), the "
                        "automata and occupant groups by ID and by name (6, 7, 11, 12), the drive command (14) and "
                        "the controller template by name (17) (12 detours; part s1, src/core/automata_scripts.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.automata.scripts.groups", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_CreateOccupantGroup,
                        "Our automata script system's occupant groups: CreateOccupantGroup (slot 13) and the occupant "
                        "group template's constructor, destructor body, deleting destructor, record map clear, "
                        "Matches (its slot 3), the parent chain queries and the name lookup (10 detours; part s1, "
                        "src/core/automata_scripts.cpp)",
                        false, InstallGroups}),
};

} // namespace
} // namespace opensc4::automata_scripts
