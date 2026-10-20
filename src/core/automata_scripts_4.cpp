// Our automata script system, part s3b (build 641): the last readers of the
// scripts' loading (LoadScripts, part s3a, runs them): the occupant groups
// (0x005216A0, the global table `occupant_group`) and the automata groups
// (0x00521C70, `automata_group`), both through the group reader 0x00520DF0
// (the group made or found by name, its ID, percentage, controllers,
// models (0x0051ECA0), animations, parent reference, class and own
// reference); the automata groups' trailers, speeds, drive parameters (the
// airplane 0x0051E1E0, the helicopter 0x0051E4A0, the watercraft
// 0x0051E6B0 and the player's driving 0x0051DC20), image, source building
// and key effects (0x00521800, with the three-string record's copy
// 0x0051FB90 and the record map's insert 0x00520520). The layout is in
// src/core/automata_scripts.h and the survey in docs/automata.md, "Part
// s". Calls between functions of the part go to the game's addresses, so
// each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.scripts.groupreaders (7): kGroups below.
//   simulators.automata.scripts.driveparams (4): kDrive.
//
// The group's fields the readers write: +0x14 the class (`class_id`), +0x2C
// the percentage (1.0 when absent), +0x30 the controller templates
// (`controllers`, a list of template pointers), +0x34 the animations
// (`anims`, name -> number or hex), +0x48 the models (`models`, three hex
// IDs per entry, 0x14-byte nodes), +0x4C the group table's reference and
// +0x4E the parent's (`_parent`), +0x54 the trailers' names, +0x58/+0x5C the
// speeds (`kph`), +0x60 the drive parameters (0xA0 bytes, one of three
// kinds), +0x64 the player's driving (0x94 bytes), +0x68 the image and +0x6C
// the source building (hex strings), +0x70 the key effects (a byte key ->
// three-string record: fx, bone, controller and the flags hold 1, one_shot
// 2, hard_transition 4, override 8, fx 0x10, controller 0x20). The group's
// table gets its `__group_handle`.
//
// Oddities kept (all as the game does them):
// - A group's ID or class read as a number is the Lua number truncated
//   through __ftol2 (0x009EEF04); ours truncates the same way (the
//   indefinite integer's low word, 0, for a NaN or an out-of-range number).
// - The models reader reads three strings of each entry whether it has
//   them or not.
// - The key effects' key is read only when the entry's walk has an index;
//   ours starts it at 0 where the game's local is a leftover (the walk always
//   has one).
// - The drive parameter readers stop at the first field read without an
//   index (never: the walk's index is fixed); the player's driving reads
//   `OffRoadTurnAngleRange` even then.
// - A walk over a key-effect entry is made only for its index.
//
// Floats: the Lua numbers stored as floats are rounded at the store (the
// field readers, the group's percentage).
#include "core/automata_scripts.h"

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
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

void NewEmptyString(GameString& s)
{
    s.begin = s.end = s.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    s.begin = buffer;
    s.end = buffer;
    s.capacity = buffer + 8;
    *buffer = 0;
    s.vtable = addr::SC4String_Vtable;
    s.extra = 0;
}

void FreeString(GameString& s)
{
    s.vtable = addr::SC4String_Vtable;
    if (s.begin)
        Free(s.begin);
}

// A game string copied from a C string as the readers inline it.
void CopyCString(GameString& s, const char* text)
{
    s.begin = s.end = s.capacity = nullptr;
    const std::uint32_t length = static_cast<std::uint32_t>(std::strlen(text));
    const std::uint32_t size = length + 1;
    char* buffer = nullptr;
    if (size == 0) {
        CallCdecl<void>(addr::STL_ThrowLengthError, Text(addr::STL_BasicStringText));
        buffer = s.begin;
    } else {
        buffer = static_cast<char*>(Allocate(size));
        s.begin = buffer;
        s.end = buffer;
        s.capacity = buffer + size;
    }
    if (length)
        std::memmove(buffer, text, length);
    s.end = buffer + length;
    *s.end = 0;
    s.vtable = addr::SC4String_Vtable;
    s.extra = 0;
}

void* Context(char* self) { return At<void*>(self, field::kObject18); }

bool GetField(void* context, std::uintptr_t name, std::int32_t index)
{
    return CallCdecl<std::uint8_t>(addr::SC4Script_GetField, context, Text(name), index) != 0;
}

void Pop(void* context) { Virtual<std::uint32_t>(context, 0x2C, 1); }

struct TableWalk {
    alignas(4) unsigned char bytes[0x20];
    std::int32_t Index() { return At<std::int32_t>(bytes, 0x18); }
    bool Done() { return At<std::uint8_t>(bytes, 0x1C) != 0; }
    void DestroyString()
    {
        At<std::uintptr_t>(bytes, 4) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(bytes, 8))
            Free(buffer);
    }
};

void WalkAt(TableWalk& walk, void* context, std::int32_t index)
{
    CallThis<void*>(addr::SC4ScriptTableWalk_Construct, walk.bytes, context, index);
}

void ListAppend(char* head, void* value)
{
    char* const node = static_cast<char*>(Allocate(0xC));
    if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0)
        At<void*>(node, 8) = value;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
}

void ClearListNodes(char* head)
{
    char* node = At<char*>(head, 0);
    while (node != head) {
        char* const next = At<char*>(node, 0);
        Free(node);
        node = next;
    }
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
}

// __ftol2 (0x009EEF04): the number truncated to 64 bits, the low word
// answered; the indefinite integer (0x8000000000000000) for a NaN or a
// number out of range.
std::uint32_t Ftol(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

// A value at the top of the stack as an ID: a number truncated, else a
// string read as hex.
std::uint32_t TopAsId(void* context)
{
    if (Virtual<std::uint8_t>(context, 0x38, -1))
        return Ftol(static_cast<double>(Virtual<long double>(context, 0x5C, -1)));
    const char* const text = Virtual<const char*>(context, 0x60, -1);
    return CallCdecl<std::uint32_t>(addr::SC4_strtoul, text, static_cast<char**>(nullptr), 16);
}

std::uint32_t HexOf(GameString& s)
{
    const char* const text = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&s));
    return CallCdecl<std::uint32_t>(addr::SC4_strtoul, text, static_cast<char**>(nullptr), 16);
}

struct NamePair {
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t value;
};

struct InsertResult {
    void* node;
    void* table;
    std::uint8_t inserted;
    std::uint8_t pad[3];
};

// ====================================================================================
// The groups

// 0x0051ECA0 (index, list), ret 8: the models: the list (a field holding
// its head; 0x14-byte nodes) emptied, then per entry of the table at the
// index its three strings read as hex.
std::uint8_t __fastcall ReadModels(char* self, void*, std::int32_t index, char** list)
{
    static bool logged;
    FirstCall("ReadModels", logged);
    ClearListNodes(*list);
    TableWalk walk;
    WalkAt(walk, Context(self), static_cast<std::int16_t>(index));
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        struct {
            char* begin;
            char* end;
            char* capacity;
        } strings{nullptr, nullptr, nullptr};
        CallCdecl<void>(addr::SC4ScriptValue_StringVector, Context(self), static_cast<void*>(&strings), -1);
        std::uint32_t ids[3];
        for (int i = 0; i < 3; ++i) {
            const char* const text = Virtual<const char*>(strings.begin + 0x14 * i, 0x14);
            ids[i] = CallCdecl<std::uint32_t>(addr::SC4_strtoul, text, static_cast<char**>(nullptr), 16);
        }
        char* const head = *list;
        char* const node = static_cast<char*>(Allocate(0x14));
        if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0) {
            At<std::uint32_t>(node, 0x08) = ids[0];
            At<std::uint32_t>(node, 0x0C) = ids[1];
            At<std::uint32_t>(node, 0x10) = ids[2];
        }
        char* const last = At<char*>(head, 4);
        At<char*>(node, 0) = head;
        At<char*>(node, 4) = last;
        At<char*>(last, 0) = node;
        At<char*>(head, 4) = node;
        for (char* s = strings.begin; s != strings.end; s += 0x14)
            Virtual<std::uint32_t>(s, 0x70, 0u);
        if (strings.begin)
            Free(strings.begin);
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    walk.DestroyString();
    return 1;
}

// 0x00520DF0 (name, index), ret 8: a group read from the table at the index
// (only the index's low word counts): see the top of the file. Answers the
// group, or null when there is none to make.
char* __fastcall ReadGroup(char* self, void*, const void* name, std::int32_t indexArgument)
{
    static bool logged;
    FirstCall("ReadGroup", logged);
    void* const context = Context(self);
    const std::int32_t index = static_cast<std::int16_t>(indexArgument);
    std::uint32_t id = 0;
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_group_id, index)) {
        id = TopAsId(context);
        Pop(context);
    }
    const void* const key = name ? static_cast<const char*>(name) + 4 : nullptr;
    char* g = nullptr;
    if (char* const found = CallThis<char*>(addr::SC4StringHashSet_Find, self + field::kGroupsByName, key))
        g = At<char*>(found, 0x10);
    else
        g = Virtual<char*>(self, 0x34, name, id, 0u);
    if (!g)
        return nullptr;
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_percentage, index)) {
        At<float>(g, group::kPercentage) = static_cast<float>(Virtual<long double>(context, 0x5C, -1));
        Pop(context);
    } else {
        At<std::uint32_t>(g, group::kPercentage) = 0x3F800000u;
    }
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_controllers, index)) {
        struct {
            char* begin;
            char* end;
            char* capacity;
        } strings{nullptr, nullptr, nullptr};
        CallCdecl<void>(addr::SC4ScriptValue_StringVector, context, static_cast<void*>(&strings), -1);
        for (char* s = strings.begin; s != strings.end; s += 0x14)
            if (void* const t = Virtual<void*>(self, 0x44, static_cast<void*>(s)))
                ListAppend(At<char*>(g, group::kList30), t);
        Pop(context);
        for (char* s = strings.begin; s != strings.end; s += 0x14)
            Virtual<std::uint32_t>(s, 0x70, 0u);
        if (strings.begin)
            Free(strings.begin);
    } else {
        CallThis<void>(addr::SC4StlList_Clear, g + group::kList30);
    }
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_models, index)) {
        CallThis<std::uint8_t>(addr::cSC4AutomataScriptSystem_ReadModels, self, -1, g + group::kList48);
        Pop(context);
    } else {
        ClearListNodes(At<char*>(g, group::kList48));
    }
    char* const names = g + group::kNames;
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_anims, index)) {
        CallThis<void>(addr::SC4HashTable_Clear, names);
        TableWalk walk;
        WalkAt(walk, context, -1);
        CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
        while (!walk.Done()) {
            const char* const text = Virtual<const char*>(context, 0x60, -2);
            const std::uint32_t length = static_cast<std::uint32_t>(std::strlen(text));
            char* first = nullptr;
            if (length + 1 == 0)
                CallCdecl<void>(addr::STL_ThrowLengthError, Text(addr::STL_BasicStringText));
            else
                first = static_cast<char*>(Allocate(length + 1));
            char* end = first;
            if (length) {
                std::memmove(first, text, length);
                end = first + length;
            }
            *end = 0;
            std::uint32_t value = 0;
            if (Virtual<std::uint8_t>(context, 0x38, -1))
                value = Ftol(static_cast<double>(Virtual<long double>(context, 0x5C, -1)));
            else if (Virtual<std::uint8_t>(context, 0x3C, -1))
                value = CallCdecl<std::uint32_t>(addr::SC4_strtoul, Virtual<const char*>(context, 0x60, -1),
                                                 static_cast<char**>(nullptr), 16);
            const std::uint32_t used = static_cast<std::uint32_t>(end - first);
            NamePair pair{nullptr, nullptr, nullptr, 0};
            if (used + 1 == 0) {
                CallCdecl<void>(addr::STL_ThrowLengthError, Text(addr::STL_BasicStringText));
            } else {
                pair.begin = static_cast<char*>(Allocate(used + 1));
                pair.capacity = pair.begin + used + 1;
            }
            char* out = pair.begin;
            for (std::uint32_t i = 0; static_cast<std::int32_t>(i) < static_cast<std::int32_t>(used); ++i)
                *out++ = first[i];
            *out = 0;
            pair.end = out;
            pair.value = value;
            CallThis<void>(addr::SC4StringHash_Grow, names, At<std::uint32_t>(names, 0x10) + 1);
            InsertResult result{};
            CallThis<void*>(addr::SC4StringHashMap_InsertUnique, names, static_cast<void*>(&result),
                            static_cast<const void*>(&pair));
            if (pair.begin)
                Free(pair.begin);
            if (first)
                Free(first);
            CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
        }
        Pop(context);
        walk.DestroyString();
    } else {
        CallThis<void>(addr::SC4HashTable_Clear, names);
    }
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key__parent, index)) {
        if (At<std::int16_t>(g, group::kWord4E) != -2)
            Virtual<std::uint32_t>(context, 0xF8, static_cast<std::int32_t>(At<std::int16_t>(g, group::kWord4E)));
        At<std::int16_t>(g, group::kWord4E) = static_cast<std::int16_t>(Virtual<std::uint32_t>(context, 0xC8, 0));
    }
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_class_id, index)) {
        At<std::uint32_t>(g, group::kClass) = TopAsId(context);
        Pop(context);
    } else {
        At<std::uint32_t>(g, group::kClass) = 0;
    }
    Virtual<std::uint32_t>(context, 0x84, Global<const char*>(addr::cSC4AutomataScriptSystem_GroupHandleName));
    Virtual<std::uint32_t>(context, 0x114, static_cast<void*>(g), 0u);
    Virtual<std::uint32_t>(context, 0xBC, index);
    if (At<std::int16_t>(g, group::kWord4C) != -2)
        Virtual<std::uint32_t>(context, 0xF8, static_cast<std::int32_t>(At<std::int16_t>(g, group::kWord4C)));
    Virtual<std::uint32_t>(context, 0x20, index);
    At<std::int16_t>(g, group::kWord4C) = static_cast<std::int16_t>(Virtual<std::uint32_t>(context, 0xC8, 0));
    return g;
}

// 0x005216A0: the global table `occupant_group` (0x00B09A34): a group per
// entry (0x00520DF0 with the entry's index).
std::uint8_t __fastcall ReadOccupantGroups(char* self, void*)
{
    static bool logged;
    FirstCall("ReadOccupantGroups", logged);
    void* const context = Context(self);
    TableWalk walk;
    CallThis<void*>(addr::SC4ScriptTableWalk_ConstructGlobal, walk.bytes, context,
                    Global<const char*>(addr::cSC4AutomataScriptSystem_OccupantGroupTableName));
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        GameString name;
        CopyCString(name, Virtual<const char*>(Context(self), 0x60, -2));
        TableWalk entry;
        WalkAt(entry, Context(self), -1);
        CallThis<char*>(addr::cSC4AutomataScriptSystem_ReadGroup, self, static_cast<const void*>(&name), entry.Index());
        entry.DestroyString();
        FreeString(name);
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    Pop(Context(self));
    walk.DestroyString();
    return 1;
}

// 0x0051FB90 (from), ret 4: the three-string record's copy constructor
// (each string from the other's STL part, SC4String::CopyBuffer 0..npos),
// +0x3C copied.
char* __fastcall Record3Copy(char* self, void*, char* from)
{
    static bool logged;
    FirstCall("Record3Copy", logged);
    std::uint32_t tag = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(from)); // the argument's slot
    for (const std::size_t at : {std::size_t{0}, std::size_t{0x14}, std::size_t{0x28}}) {
        const char* const source = (at == 0 && !from) ? nullptr : from + at + 4;
        CallThis<void*>(addr::SC4String_CopyBuffer, self + at + 4, source, 0u, 0xFFFFFFFFu, static_cast<void*>(&tag));
        At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
        At<std::uint32_t>(self, at + 0x10) = 0;
    }
    At<std::uint32_t>(self, 0x3C) = At<std::uint32_t>(from, 0x3C);
    return self;
}

// 0x00520520 (this = a group's record map; result, pair), ret 8: the
// insert of a (key byte, record) pair unless the key is there; a 0x48-byte
// node at its bucket's head, the count up.
void* __fastcall RecordMapInsert(char* self, void*, InsertResult* result, const char* pair)
{
    static bool logged;
    FirstCall("RecordMapInsert", logged);
    const std::uint8_t key = At<std::uint8_t>(const_cast<char*>(pair), 0);
    const std::int32_t bytes = static_cast<std::int32_t>(At<char*>(self, 8) - At<char*>(self, 4));
    const std::uint32_t bucket = static_cast<std::uint32_t>(key) % static_cast<std::uint32_t>(bytes >> 2);
    char* const first = At<char**>(self, 4)[bucket];
    for (char* node = first; node; node = At<char*>(node, 0))
        if (At<std::uint8_t>(node, 4) == key) {
            result->table = self;
            result->node = node;
            result->inserted = 0;
            return result;
        }
    char* const node = static_cast<char*>(Allocate(0x48));
    At<char*>(node, 0) = nullptr;
    if (reinterpret_cast<std::uintptr_t>(node) + 4 != 0) {
        At<std::uint8_t>(node, 4) = key;
        CallThis<char*>(addr::cSC4OccupantGroupTemplate_Record3Copy, node + 8, const_cast<char*>(pair) + 4);
    }
    At<char*>(node, 0) = first;
    At<char**>(self, 4)[bucket] = node;
    ++At<std::uint32_t>(self, 0x10);
    result->node = node;
    result->table = self;
    result->inserted = 1;
    return result;
}

// The three-string record's destruction the key effects reader inlines.
void DestroyRecord3Inline(char* record)
{
    for (const std::size_t at : {std::size_t{0x28}, std::size_t{0x14}, std::size_t{0}}) {
        At<std::uintptr_t>(record, at) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(record, at + 4))
            Free(buffer);
    }
}

// 0x00521800 (index, group), ret 8: the key effects: per table entry of the
// table at the index, a three-string record (cmd the key byte, fx, bone,
// controller and four flags) into the group's record map (+0x70) when its
// fx is not empty.
std::uint8_t __fastcall ReadKeyEffects(char* self, void*, std::int32_t index, char* g)
{
    static bool logged;
    FirstCall("ReadKeyEffects", logged);
    TableWalk walk;
    WalkAt(walk, Context(self), index);
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        if (Virtual<std::uint8_t>(Context(self), 0x120, -1)) {
            alignas(4) char record[0x40];
            CallThis<char*>(addr::cSC4OccupantGroupTemplate_Record3Construct, record);
            TableWalk entry;
            WalkAt(entry, Context(self), -1);
            At<std::uint32_t>(record, 0x3C) = 0;
            void* const context = Context(self);
            std::uint32_t command = 0;
            if (entry.Index() != -1)
                CallCdecl<std::uint8_t>(addr::SC4ScriptField_Uint32, context, Text(addr::cSC4AutomataScriptSystem_Key_cmd),
                                        static_cast<void*>(&command), entry.Index());
            if (entry.Index() != -1)
                CallCdecl<void>(addr::SC4ScriptField_String, context, Text(addr::cSC4AutomataScriptSystem_Key_fx),
                                static_cast<void*>(record), entry.Index());
            if (At<char*>(record, 8) != At<char*>(record, 4))
                At<std::uint32_t>(record, 0x3C) |= 0x10;
            if (entry.Index() != -1) {
                CallCdecl<void>(addr::SC4ScriptField_String, context, Text(addr::cSC4AutomataScriptSystem_Key_bone),
                                static_cast<void*>(record + 0x14), entry.Index());
                if (entry.Index() != -1)
                    CallCdecl<void>(addr::SC4ScriptField_String, context,
                                    Text(addr::cSC4AutomataScriptSystem_Key_controller),
                                    static_cast<void*>(record + 0x28), entry.Index());
            }
            if (At<char*>(record, 0x30) != At<char*>(record, 0x2C))
                At<std::uint32_t>(record, 0x3C) |= 0x20;
            struct Flag {
                std::uintptr_t name;
                std::uint32_t bit;
            };
            static constexpr Flag kFlags[] = {
                {addr::cSC4AutomataScriptSystem_Key_hold, 1},
                {addr::cSC4AutomataScriptSystem_Key_one_shot, 2},
                {addr::cSC4AutomataScriptSystem_Key_hard_transition, 4},
                {addr::cSC4AutomataScriptSystem_Key_override, 8},
            };
            for (const Flag& f : kFlags) {
                std::uint8_t on = 0;
                if (entry.Index() != -1) {
                    CallCdecl<std::uint8_t>(addr::SC4ScriptField_Bool, context, Text(f.name), static_cast<void*>(&on),
                                            entry.Index());
                    if (on)
                        At<std::uint32_t>(record, 0x3C) |= f.bit;
                }
            }
            if (At<char*>(record, 8) != At<char*>(record, 4)) {
                alignas(4) char pair[0x44];
                At<std::uint8_t>(pair, 0) = static_cast<std::uint8_t>(command);
                CallThis<char*>(addr::cSC4OccupantGroupTemplate_Record3Copy, pair + 4, record);
                char* const records = g + group::kRecords;
                CallThis<void>(addr::SC4HashTable_Resize, records, At<std::uint32_t>(records, 0x10) + 1);
                InsertResult result{};
                CallThis<void*>(addr::cSC4OccupantGroupTemplate_RecordMapInsert, records, static_cast<void*>(&result),
                                static_cast<const void*>(pair));
                DestroyRecord3Inline(pair + 4);
            }
            entry.DestroyString();
            DestroyRecord3Inline(record);
        }
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    walk.DestroyString();
    return 1;
}

// A drive parameter reader's field: the name, the offset in the parameter
// block and whether it is an int (SC4ScriptField::Sint32) or a float.
struct Field {
    std::uintptr_t name;
    std::size_t offset;
    bool integer;
};

const Field kAirplane[] = {
    {addr::cSC4AutomataScriptSystem_Key_Mass, 0x00, false},
    {addr::cSC4AutomataScriptSystem_Key_WingArea, 0x04, false},
    {addr::cSC4AutomataScriptSystem_Key_TakeOffSpeed, 0x08, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxSpeed, 0x0C, false},
    {addr::cSC4AutomataScriptSystem_Key_ElevatorMax, 0x10, false},
    {addr::cSC4AutomataScriptSystem_Key_ElevatorChangeRate, 0x14, false},
    {addr::cSC4AutomataScriptSystem_Key_AileronMax, 0x18, false},
    {addr::cSC4AutomataScriptSystem_Key_AileronChangeRate, 0x1C, false},
    {addr::cSC4AutomataScriptSystem_Key_RudderMax, 0x20, false},
    {addr::cSC4AutomataScriptSystem_Key_RudderChangeRate, 0x24, false},
    {addr::cSC4AutomataScriptSystem_Key_ThrottleMax, 0x28, false},
    {addr::cSC4AutomataScriptSystem_Key_ThrottleChangeRate, 0x2C, false},
    {addr::cSC4AutomataScriptSystem_Key_FlapsMax, 0x30, false},
    {addr::cSC4AutomataScriptSystem_Key_FlapsChangeRate, 0x34, false},
    {addr::cSC4AutomataScriptSystem_Key_StallAngle, 0x38, false},
    {addr::cSC4AutomataScriptSystem_Key_LiftFactorLevelFlight, 0x3C, false},
    {addr::cSC4AutomataScriptSystem_Key_LiftFactorMaxClimb, 0x40, false},
    {addr::cSC4AutomataScriptSystem_Key_LiftFactorFullStall, 0x44, false},
};
const Field kHelicopter[] = {
    {addr::cSC4AutomataScriptSystem_Key_Mass, 0x00, false},
    {addr::cSC4AutomataScriptSystem_Key_Power, 0x04, false},
    {addr::cSC4AutomataScriptSystem_Key_SpeedMultiplier, 0x08, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxSpeed, 0x0C, false},
    {addr::cSC4AutomataScriptSystem_Key_DragFactor, 0x10, false},
    {addr::cSC4AutomataScriptSystem_Key_RotationalDragFactor, 0x14, false},
    {addr::cSC4AutomataScriptSystem_Key_BankMax, 0x18, false},
    {addr::cSC4AutomataScriptSystem_Key_BankChangeRate, 0x1C, false},
    {addr::cSC4AutomataScriptSystem_Key_PitchMax, 0x20, false},
    {addr::cSC4AutomataScriptSystem_Key_PitchChangeRate, 0x24, false},
    {addr::cSC4AutomataScriptSystem_Key_YawRateMax, 0x28, false},
    {addr::cSC4AutomataScriptSystem_Key_BankMultiplier, 0x2C, false},
    {addr::cSC4AutomataScriptSystem_Key_PitchMultiplier, 0x30, false},
};
const Field kWatercraft[] = {
    {addr::cSC4AutomataScriptSystem_Key_DecelPerSecond, 0x00, false},
    {addr::cSC4AutomataScriptSystem_Key_TurnsPerSecond, 0x04, false},
    {addr::cSC4AutomataScriptSystem_Key_RestoreTurnsPerSecond, 0x08, false},
    {addr::cSC4AutomataScriptSystem_Key_DecaySpeedPerSecond, 0x0C, false},
    {addr::cSC4AutomataScriptSystem_Key_SpeedModifier, 0x10, false},
    {addr::cSC4AutomataScriptSystem_Key_SwitchTurnDirectionsModifier, 0x14, false},
    {addr::cSC4AutomataScriptSystem_Key_MinSpeed, 0x18, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxSpeed, 0x1C, false},
    {addr::cSC4AutomataScriptSystem_Key_MinTurn, 0x20, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxTurn, 0x24, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxSpeedChangePerMillisecond, 0x28, false},
    {addr::cSC4AutomataScriptSystem_Key_BaseForwardTurnRadius, 0x2C, false},
    {addr::cSC4AutomataScriptSystem_Key_BaseBackwardTurnRadius, 0x30, false},
    {addr::cSC4AutomataScriptSystem_Key_AIMaxSpeedChangePerMillisecond, 0x34, false},
    {addr::cSC4AutomataScriptSystem_Key_AIMaxTurnChangePerMillisecond, 0x38, false},
    {addr::cSC4AutomataScriptSystem_Key_AICrashSpeedAnimateCollisionThreshold, 0x3C, false},
    {addr::cSC4AutomataScriptSystem_Key_AICrashSpeedInanimateCollisionThreshold, 0x40, false},
    {addr::cSC4AutomataScriptSystem_Key_ForwardMotionPitchModifier, 0x44, false},
    {addr::cSC4AutomataScriptSystem_Key_ForwardMotionPitchMaxAngle, 0x48, false},
    {addr::cSC4AutomataScriptSystem_Key_SideMotionPitchModifier, 0x4C, false},
    {addr::cSC4AutomataScriptSystem_Key_SideMotionPitchMaxAngle, 0x50, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashSequenceMillisecondMax, 0x54, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashPitchTotal, 0x58, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashRunAgroundSpeedReductionModifier, 0x5C, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashRunAgroundRollTotal, 0x60, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashClimableTerrainAngleThreshold, 0x64, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashMaxAltitudeIncrease, 0x68, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashSpeedAnimateCollisionThreshold, 0x6C, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashSpeedInanimateCollisionThreshold, 0x70, false},
    {addr::cSC4AutomataScriptSystem_Key_CrashAnimateCollisionSpeedScalingFactor, 0x74, false},
    {addr::cSC4AutomataScriptSystem_Key_MinAcceleration, 0x78, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxAcceleration, 0x7C, false},
    {addr::cSC4AutomataScriptSystem_Key_ReverseInertialTime, 0x80, false},
    {addr::cSC4AutomataScriptSystem_Key_ForwardInertialTime, 0x84, false},
    {addr::cSC4AutomataScriptSystem_Key_TurnInPlacePerSec, 0x88, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxMillisecondDelta, 0x8C, false},
    {addr::cSC4AutomataScriptSystem_Key_FerryQueueDistance, 0x90, false},
    {addr::cSC4AutomataScriptSystem_Key_FerryDockDistance, 0x94, false},
    {addr::cSC4AutomataScriptSystem_Key_FerryMaxDockingOrientationChangePerMillisecond, 0x98, false},
    {addr::cSC4AutomataScriptSystem_Key_FerryMaxDockingPositionChangePerMillisecond, 0x9C, false},
};
const Field kDriving[] = {
    {addr::cSC4AutomataScriptSystem_Key_MinZoom, 0x00, true},
    {addr::cSC4AutomataScriptSystem_Key_StartingZoom, 0x04, true},
    {addr::cSC4AutomataScriptSystem_Key_MaxSpeed, 0x08, false},
    {addr::cSC4AutomataScriptSystem_Key_MinSpeed, 0x0C, false},
    {addr::cSC4AutomataScriptSystem_Key_AccelPerSecond, 0x10, false},
    {addr::cSC4AutomataScriptSystem_Key_DecelPerSecond, 0x14, false},
    {addr::cSC4AutomataScriptSystem_Key_DecaySpeedPerSec, 0x1C, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxTurnAngle, 0x20, false},
    {addr::cSC4AutomataScriptSystem_Key_TurnPerSec, 0x2C, false},
    {addr::cSC4AutomataScriptSystem_Key_RestoreTurnPerSec, 0x30, false},
    {addr::cSC4AutomataScriptSystem_Key_TurnAngleThreshhold, 0x34, false},
    {addr::cSC4AutomataScriptSystem_Key_GripPercentage, 0x38, false},
    {addr::cSC4AutomataScriptSystem_Key_GuardrailDotTolerance, 0x3C, false},
    {addr::cSC4AutomataScriptSystem_Key_GuardrailDistance, 0x40, false},
    {addr::cSC4AutomataScriptSystem_Key_GuardrailPercentage, 0x44, false},
    {addr::cSC4AutomataScriptSystem_Key_OffRoadSpeedModifier, 0x18, false},
    {addr::cSC4AutomataScriptSystem_Key_RelativeDensity, 0x48, false},
    {addr::cSC4AutomataScriptSystem_Key_TrainDerailmentThrottleDuration, 0x4C, false},
    {addr::cSC4AutomataScriptSystem_Key_TrainFullThrottlePitchMax, 0x50, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxTapDuration, 0x54, false},
    {addr::cSC4AutomataScriptSystem_Key_TankTurnInPlace, 0x58, false},
    {addr::cSC4AutomataScriptSystem_Key_HitPoints, 0x5C, false},
    {addr::cSC4AutomataScriptSystem_Key_AccelRate, 0x60, false},
    {addr::cSC4AutomataScriptSystem_Key_BankRate, 0x64, false},
    {addr::cSC4AutomataScriptSystem_Key_ClimbRate, 0x68, false},
    {addr::cSC4AutomataScriptSystem_Key_DiveRate, 0x6C, false},
    {addr::cSC4AutomataScriptSystem_Key_RudderRate, 0x70, false},
    {addr::cSC4AutomataScriptSystem_Key_ReturnToCenterFactor, 0x74, false},
    {addr::cSC4AutomataScriptSystem_Key_MinThrottle, 0x78, false},
    {addr::cSC4AutomataScriptSystem_Key_FlapRateClimb, 0x7C, false},
    {addr::cSC4AutomataScriptSystem_Key_FlapRateDive, 0x80, false},
    {addr::cSC4AutomataScriptSystem_Key_LandingThrottle, 0x84, false},
    {addr::cSC4AutomataScriptSystem_Key_LandingElevator, 0x88, false},
    {addr::cSC4AutomataScriptSystem_Key_TrimRate, 0x8C, false},
    {addr::cSC4AutomataScriptSystem_Key_MaxTrim, 0x90, false},
};

// The drive parameter readers' fields read in order while the walk has an
// index (re-read before each, as the game does); false when one is read
// without one.
bool ReadFields(void* context, TableWalk& walk, char* block, const Field* fields, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (walk.Index() == -1)
            return false;
        const std::uintptr_t reader = fields[i].integer ? addr::SC4ScriptField_Sint32 : addr::SC4ScriptField_Float;
        CallCdecl<std::uint8_t>(reader, context, Text(fields[i].name), static_cast<void*>(block + fields[i].offset),
                                walk.Index());
    }
    return true;
}

std::uint8_t ReadParams(char* self, std::int32_t index, char* block, const Field* fields, std::size_t count)
{
    TableWalk walk;
    WalkAt(walk, Context(self), index);
    if (!walk.Done())
        ReadFields(Context(self), walk, block, fields, count);
    walk.DestroyString();
    return 1;
}

// 0x0051E1E0 (index, block), ret 8: the airplane's drive parameters.
std::uint8_t __fastcall ReadAirplane(char* self, void*, std::int32_t index, char* block)
{
    static bool logged;
    FirstCall("ReadAirplane", logged);
    return ReadParams(self, index, block, kAirplane, sizeof kAirplane / sizeof kAirplane[0]);
}

// 0x0051E4A0 (index, block), ret 8: the helicopter's.
std::uint8_t __fastcall ReadHelicopter(char* self, void*, std::int32_t index, char* block)
{
    static bool logged;
    FirstCall("ReadHelicopter", logged);
    return ReadParams(self, index, block, kHelicopter, sizeof kHelicopter / sizeof kHelicopter[0]);
}

// 0x0051E6B0 (index, block), ret 8: the watercraft's.
std::uint8_t __fastcall ReadWatercraft(char* self, void*, std::int32_t index, char* block)
{
    static bool logged;
    FirstCall("ReadWatercraft", logged);
    return ReadParams(self, index, block, kWatercraft, sizeof kWatercraft / sizeof kWatercraft[0]);
}

// 0x0051DC20 (index, block), ret 8: the player's driving: the fields, then
// `OffRoadTurnAngleRange` (two numbers into +0x24, +0x28) at the walk's
// index (also when a field stopped the reading).
std::uint8_t __fastcall ReadDriving(char* self, void*, std::int32_t index, char* block)
{
    static bool logged;
    FirstCall("ReadDriving", logged);
    void* const context = Context(self);
    TableWalk walk;
    WalkAt(walk, context, index);
    if (!walk.Done()) {
        ReadFields(context, walk, block, kDriving, sizeof kDriving / sizeof kDriving[0]);
        if (GetField(context, addr::cSC4AutomataScriptSystem_Key_OffRoadTurnAngleRange, walk.Index())) {
            struct {
                std::uint32_t* begin;
                std::uint32_t* end;
                std::uint32_t* capacity;
            } values{nullptr, nullptr, nullptr};
            CallCdecl<void>(addr::SC4ScriptValue_FloatVector, context, static_cast<void*>(&values), -1);
            if (values.begin != values.end)
                At<std::uint32_t>(block, 0x24) = values.begin[0];
            if (static_cast<std::uint32_t>(values.end - values.begin) > 1)
                At<std::uint32_t>(block, 0x28) = values.begin[1];
            Pop(context);
            if (values.begin)
                Free(values.begin);
        }
    }
    walk.DestroyString();
    return 1;
}

// 0x00521C70: the global table `automata_group` (0x00B09A38): per entry the
// group (0x00520DF0), put into the automata group map (+0x60; a reference
// when it went in), then its trailers, speeds, drive parameters, driving,
// image, source building and key effects.
std::uint8_t __fastcall ReadAutomataGroups(char* self, void*)
{
    static bool logged;
    FirstCall("ReadAutomataGroups", logged);
    TableWalk walk;
    CallThis<void*>(addr::SC4ScriptTableWalk_ConstructGlobal, walk.bytes, Context(self),
                    Global<const char*>(addr::cSC4AutomataScriptSystem_AutomataGroupTableName));
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        void* const context = Context(self);
        GameString name;
        CopyCString(name, Virtual<const char*>(context, 0x60, -2));
        TableWalk entry;
        WalkAt(entry, context, -1);
        const std::int32_t index = entry.Index();
        char* const g =
            CallThis<char*>(addr::cSC4AutomataScriptSystem_ReadGroup, self, static_cast<const void*>(&name), index);
        if (g) {
            NamePair pair{nullptr, nullptr, nullptr, 0};
            std::uint8_t allocatorTag = 0;
            CallThis<void>(addr::cSC4Advice_F408420, &pair, static_cast<const char*>(name.begin),
                           static_cast<const char*>(name.end), static_cast<void*>(&allocatorTag));
            char* const automata = self + field::kAutomataGroups;
            pair.value = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g));
            CallThis<void>(addr::SC4StringHash_Grow, automata, At<std::uint32_t>(automata, 0x10) + 1);
            InsertResult result{};
            CallThis<void*>(addr::SC4StringHashMap_InsertUnique, automata, static_cast<void*>(&result),
                            static_cast<const void*>(&pair));
            if (pair.begin)
                Free(pair.begin);
            if (result.inserted)
                Virtual<std::uint32_t>(g, 0x04);
            if (GetField(context, addr::cSC4AutomataScriptSystem_Key_trailers, index)) {
                CallCdecl<void>(addr::SC4ScriptValue_StringList, context, static_cast<void*>(g + 0x54), -1);
                Pop(context);
            }
            At<std::uint32_t>(g, 0x58) = 0;
            At<std::uint32_t>(g, 0x5C) = 0;
            if (GetField(context, addr::cSC4AutomataScriptSystem_Key_kph, index)) {
                struct {
                    std::uint32_t* begin;
                    std::uint32_t* end;
                    std::uint32_t* capacity;
                } values{nullptr, nullptr, nullptr};
                CallCdecl<void>(addr::SC4ScriptValue_FloatVector, context, static_cast<void*>(&values), -1);
                if (values.begin != values.end)
                    At<std::uint32_t>(g, 0x58) = values.begin[0];
                if (static_cast<std::uint32_t>(values.end - values.begin) > 1)
                    At<std::uint32_t>(g, 0x5C) = values.begin[1];
                Pop(context);
                if (values.begin)
                    Free(values.begin);
            }
            if (void* const old = At<void*>(g, 0x60)) {
                CallCdecl<void>(addr::SC4_operator_delete, old);
                At<void*>(g, 0x60) = nullptr;
            }
            struct Kind {
                std::uintptr_t name;
                std::uintptr_t reader;
            };
            static constexpr Kind kKinds[] = {
                {addr::cSC4AutomataScriptSystem_Key_airplane, addr::cSC4AutomataScriptSystem_ReadAirplane},
                {addr::cSC4AutomataScriptSystem_Key_helicopter, addr::cSC4AutomataScriptSystem_ReadHelicopter},
                {addr::cSC4AutomataScriptSystem_Key_WatercraftDriveParams, addr::cSC4AutomataScriptSystem_ReadWatercraft},
            };
            using Reader = std::uint8_t(__thiscall*)(void*, std::int32_t, void*);
            for (const Kind& k : kKinds)
                if (GetField(context, k.name, index)) {
                    At<void*>(g, 0x60) = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0xA0));
                    TableWalk params;
                    WalkAt(params, context, -1);
                    reinterpret_cast<Reader>(k.reader)(self, params.Index(), At<void*>(g, 0x60));
                    Pop(context);
                    params.DestroyString();
                    break;
                }
            if (void* const old = At<void*>(g, 0x64)) {
                CallCdecl<void>(addr::SC4_operator_delete, old);
                At<void*>(g, 0x64) = nullptr;
            }
            if (GetField(context, addr::cSC4AutomataScriptSystem_Key_playerdrive, index)) {
                At<void*>(g, 0x64) = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0x94));
                TableWalk params;
                WalkAt(params, context, -1);
                reinterpret_cast<Reader>(addr::cSC4AutomataScriptSystem_ReadDriving)(self, params.Index(),
                                                                                    At<void*>(g, 0x64));
                Pop(context);
                params.DestroyString();
            }
            GameString image;
            NewEmptyString(image);
            CallCdecl<void>(addr::SC4ScriptField_String, context, Text(addr::cSC4AutomataScriptSystem_Key_csi_image),
                            static_cast<void*>(&image), index);
            At<std::uint32_t>(g, 0x68) = HexOf(image);
            GameString source;
            NewEmptyString(source);
            CallCdecl<void>(addr::SC4ScriptField_String, context,
                            Text(addr::cSC4AutomataScriptSystem_Key_source_building), static_cast<void*>(&source),
                            index);
            At<std::uint32_t>(g, 0x6C) = HexOf(source);
            if (GetField(context, addr::cSC4AutomataScriptSystem_Key_key_effects, index)) {
                TableWalk effects;
                WalkAt(effects, context, -1);
                CallThis<std::uint8_t>(addr::cSC4AutomataScriptSystem_ReadKeyEffects, self, effects.Index(), g);
                Pop(context);
                effects.DestroyString();
            }
            FreeString(source);
            FreeString(image);
        }
        entry.DestroyString();
        FreeString(name);
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    Pop(Context(self));
    walk.DestroyString();
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

const Detour kGroups[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadOccupantGroups, ReadOccupantGroups),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadGroup, ReadGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadAutomataGroups, ReadAutomataGroups),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadKeyEffects, ReadKeyEffects),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Record3Copy, Record3Copy),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_RecordMapInsert, RecordMapInsert),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadModels, ReadModels),
};

const Detour kDrive[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadDriving, ReadDriving),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadAirplane, ReadAirplane),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadHelicopter, ReadHelicopter),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadWatercraft, ReadWatercraft),
};

#undef OPENSC4_DETOUR

bool InstallGroups(registry::Mode) { return InstallList(kGroups, sizeof kGroups / sizeof kGroups[0], "groupreaders"); }
bool InstallDrive(registry::Mode) { return InstallList(kDrive, sizeof kDrive / sizeof kDrive[0], "driveparams"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.scripts.groupreaders", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_ReadGroup,
                        "Our automata script system's group readers: the occupant and automata groups, the group "
                        "reader, the models, the key effects with the three-string record's copy and the record "
                        "map's insert (7 detours; part s3b, src/core/automata_scripts_4.cpp)",
                        false, InstallGroups}),
    registry::Register({"simulators.automata.scripts.driveparams", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_ReadDriving,
                        "Our automata script system's drive parameter readers: the player's driving, the airplane, "
                        "the helicopter and the watercraft (4 detours; part s3b, src/core/automata_scripts_4.cpp)",
                        false, InstallDrive}),
};

} // namespace
} // namespace opensc4::automata_scripts
