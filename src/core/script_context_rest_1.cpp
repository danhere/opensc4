// The rest of the script classes, part 1 (docs/lua.md): what follows the
// script context's own code (0x00881260 to 0x00881970) and the native
// function table (0x008B59B0 to 0x008B68D0). Replacement, off by default:
//
//   scripts.context.rest1: the call closure of the data registry (the twin
//     of the value exchange closure in script_context_1.cpp), the script
//     context's handler of the registry's notifications, and the native
//     function table (class 0x2BD13FB4): constructor, destructors,
//     QueryInterface and its thunks, the setup, RemoveFunction, the Lua
//     dispatcher every registered native goes through, and the helpers of
//     its vector of records.
//
// The table's object (0x58 bytes): the counted base's vtable at +0, the
// table interface at +8 (Shutdown, AddFunction, RemoveFunction; the first
// two are in script_context_2.cpp), the setup interface at +0xC, the
// context's thread at +0x10, the vector of records at +0x14, the list of
// free record indices at +0x20, the hash map from a function's id to its
// record index at +0x24, the two parameter sets of a call at +0x38 and
// +0x3C, the owner (the script server, which runs the natives) at +0x40 and
// the name of the script table the functions go in at +0x44 (a string).
//
// A record (0x2C bytes) is the function's id, its name (a string at +4) and
// a text (a string at +0x18). The vector's helpers are the game's STL
// written out for this record; they are ours too, each with the original's
// signature, because nothing else in the game uses them.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::script_context {
namespace {

namespace context { // the script context, from the object's start
constexpr std::size_t kThread = 0x8;
constexpr std::size_t kEngine = 0x1C;
constexpr std::size_t kTarget = 0x44;
constexpr std::size_t kInput = 0x4C;  // parameter set
constexpr std::size_t kOutput = 0x50; // the same
} // namespace context

namespace table { // the native function table, from the object's start
constexpr std::size_t kTablePart = 0x8;
constexpr std::size_t kSetupPart = 0xC;
constexpr std::size_t kContext = 0x10;
constexpr std::size_t kRecords = 0x14; // vector of records
constexpr std::size_t kRecordsEnd = 0x18;
constexpr std::size_t kFreeList = 0x20; // list of free record indices
constexpr std::size_t kById = 0x24;     // hash map id -> record index
constexpr std::size_t kBuckets = 0x28;
constexpr std::size_t kBucketsEnd = 0x2C;
constexpr std::size_t kInput = 0x38;  // parameter set
constexpr std::size_t kOutput = 0x3C; // the same
constexpr std::size_t kOwner = 0x40;
constexpr std::size_t kTableName = 0x44; // string
} // namespace table

namespace record {
constexpr std::ptrdiff_t kSize = 0x2C;
constexpr std::size_t kId = 0x0;
constexpr std::size_t kName = 0x4;  // string; its buffer (begin, end, capacity) at +8
constexpr std::size_t kText = 0x18; // string; its buffer at +0x1C
} // namespace record

constexpr uint32_t kTableIid = 0x0B90251F;
constexpr uint32_t kSetupIid = 0x2BD144DF;
constexpr uint32_t kStatusParameter = 0x2B999D2B; // not handed back to the script
constexpr int32_t kRegistryIndex = -10000;        // Lua's LUA_REGISTRYINDEX
constexpr int32_t kGlobalsIndex = -10001;         // Lua's LUA_GLOBALSINDEX

// Lua 5.0's value types as Type returns them.
constexpr int32_t kLuaNil = 0;
constexpr int32_t kLuaBoolean = 1;
constexpr int32_t kLuaNumber = 3;
constexpr int32_t kLuaString = 4;
constexpr int32_t kLuaTable = 5;

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's variant (vtable 0x00ACECD0).
struct ScriptValue {
    std::uintptr_t vtable;
    uint32_t data[2];
    uint16_t word;
    uint16_t type;
    uint32_t count;
};
static_assert(sizeof(ScriptValue) == 0x14);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The original's cast between two parts of an object: `self` moved by
// `to - from` bytes, null only where the object itself (self - from) is.
char* Part(char* self, std::size_t from, std::size_t to)
{
    return reinterpret_cast<std::uintptr_t>(self) != from ? self - from + to : nullptr;
}

// An upvalue of the running closure, a light userdata.
char* Upvalue(char* thread, int32_t number)
{
    return Virtual<char*>(thread, 0xB8, Virtual<int32_t>(thread, 0x148, number));
}

// Gives the variant a copy of the text.
void SetText(ScriptValue* value, const char* characters)
{
    GameString text;
    void* const made = This<void*>(addr::SC4String_FromText, &text, characters);
    This<void>(addr::SC4ScriptNumber_SetString, value, made);
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

// The end of a string local: its buffer is freed.
void DropString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

// ---------------------------------------------------------------------------
// The game's 0x00881260 (cdecl, a Lua closure): the function behind a call
// entry of the data registry in a script. Its three upvalues are the
// registry, the entry and the script context that made it. The script's
// arguments go into the context's input parameter set, the registry calls
// the entry, and every parameter of the output set but the status is pushed
// as a result. Returns the number of results; nothing where the registry no
// longer knows the entry.
int32_t __cdecl RegistryValueCall(char* state)
{
    char* const thread = *reinterpret_cast<char**>(state - 4);
    char* const registry = Upvalue(thread, 1);
    char* const entry = Upvalue(thread, 2);
    char* const owner = Upvalue(thread, 3);
    if (!Virtual<uint8_t>(registry, 0x30, entry)) // IsValidEntry
        return 0;

    Virtual<void>(At<void*>(owner, context::kInput), 0x10, uint32_t{0}); // SetParameterCount
    Virtual<void>(At<void*>(owner, context::kOutput), 0x10, uint32_t{0});
    const int32_t top = Virtual<int32_t>(thread, 0x38);
    for (int32_t index = 1; index <= top; ++index) {
        ScriptValue value;
        This<void*>(addr::SC4ScriptNumber_Construct, &value);
        switch (Virtual<int32_t>(thread, 0x5C, index)) {
        case kLuaString:
            SetText(&value, Virtual<const char*>(thread, 0xAC, index));
            break;
        case kLuaBoolean:
            This<void>(addr::SC4ScriptNumber_SetBool, &value, Virtual<uint8_t>(thread, 0xA8, index));
            break;
        case kLuaNumber:
            This<void>(addr::SC4ScriptNumber_SetDouble, &value, Virtual<double>(thread, 0xA4, index));
            break;
        case kLuaTable: {
            // A table stands for a node of the registry: its "_e" field.
            void* node = nullptr;
            Virtual<void>(thread, 0xCC, Global<const char*>(addr::cSCLua_EntryFieldName));
            Virtual<void>(thread, 0xE8, index);
            void* const other = Virtual<void*>(thread, 0xB8, int32_t{-1});
            if (other && Virtual<uint8_t>(registry, 0x30, other) && Virtual<uint8_t>(registry, 0x34, other))
                node = Virtual<void*>(registry, 0x28, other);
            This<void>(addr::SC4ScriptNumber_SetObject, &value, node);
            Virtual<void>(thread, 0x4C, int32_t{1});
            break;
        }
        case kLuaNil:
            This<void>(addr::SC4ScriptNumber_SetObject, &value, static_cast<void*>(nullptr));
            break;
        default:
            This<void>(addr::SC4ScriptNumber_SetDouble, &value, Global<double>(addr::SC4_DoubleZero));
            break;
        }
        Virtual<void>(At<void*>(owner, context::kInput), 0x1C, static_cast<uint32_t>(index - 1), &value);
        This<void>(addr::SC4ScriptNumber_Destroy, &value);
    }

    // DoValueCall
    Virtual<uint8_t>(registry, 0x40, entry, At<void*>(owner, context::kInput), At<void*>(owner, context::kOutput));

    GameString text;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.capacity = buffer + 8;
    text.begin = buffer;
    text.end = buffer;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;

    int32_t results = 0;
    uint32_t id = Virtual<uint32_t>(At<void*>(owner, context::kOutput), 0x34); // GetFirstParameterID
    while (id != 0xFFFFFFFF) {
        if (id != kStatusParameter) {
            void* const item = Virtual<void*>(At<void*>(owner, context::kOutput), 0x18, id); // GetParameter
            bool pushed = false;
            if (item) {
                const uint32_t type = Virtual<uint32_t>(item, 0xC);
                if (type == 1) {
                    Virtual<void>(thread, 0xD4, Virtual<uint8_t>(item, 0x24));
                    pushed = true;
                } else if (type >= 2 && type <= 14) {
                    Virtual<void>(thread, 0xC4, Virtual<double>(item, 0x9C));
                    pushed = true;
                } else if (type == 16) {
                    // A node of the registry: its table, kept in the registry
                    // of the context that made this closure, moved to the
                    // calling state. Nil where the registry has no node.
                    void* const node = Virtual<void*>(registry, 0x24, Virtual<void*>(item, 0xEC));
                    if (node) {
                        char* const home = owner + context::kThread;
                        Virtual<void>(home, 0xD8, node);
                        Virtual<void>(home, 0xE8, kRegistryIndex);
                        Virtual<void>(home, 0x54, state, int32_t{1});
                        pushed = true;
                    }
                } else {
                    Virtual<uint8_t>(item, 0xDC, &text);
                    Virtual<void>(thread, 0xCC, This<const char*>(addr::cRZBaseString_Data, &text));
                    pushed = true;
                }
            }
            if (!pushed)
                Virtual<void>(thread, 0xC0);
            ++results;
        }
        id = Virtual<uint32_t>(At<void*>(owner, context::kOutput), 0x38, id); // GetNextParameterID
    }
    DropString(text);
    return results;
}

// ---------------------------------------------------------------------------
// The game's 0x00881650: slot 3 of the script context's notification target
// (+0x44), OnGameDataRegistryNotification. The script side of the registry
// is a tree of tables under the global `sc4game`: a table per node (with
// its entry in the field "_e", and kept in Lua's registry under the entry),
// a closure per value. Kind 3 adds an entry, 2 removes one, 1 drops
// everything. The result is what the thread's SetTop leaves in EAX.
uint32_t __fastcall OnRegistryNotification(char* self, void*, char* registry, int32_t kind, void* entry)
{
    char* const thread = self - context::kTarget + context::kThread;
    const int32_t top = Virtual<int32_t>(thread, 0x38);

    // The entry's parent's table on the stack and the entry's key over it:
    // its name, or the entry itself where it has none. False (with whatever
    // was found on the stack) where the parent has no table.
    const auto pushParentAndKey = [thread, registry, entry] {
        Virtual<void>(thread, 0xD8, Virtual<void*>(registry, 0x50, entry)); // GetParentNodeEntry
        Virtual<void>(thread, 0xE8, kRegistryIndex);
        if (Virtual<int32_t>(thread, 0x5C, int32_t{-1}) != kLuaTable)
            return false;
        const char* const name = Virtual<const char*>(Virtual<void*>(registry, 0x38, entry), 0x14);
        if (name && *name)
            Virtual<void>(thread, 0xCC, name);
        else
            Virtual<void>(thread, 0xD8, entry);
        return true;
    };
    // Walks the registry below `from`, dropping every node's table from
    // Lua's registry (the callback is 0x00880710).
    const auto forgetBelow = [self, registry](void* from) {
        void* pair[2] = {Part(self, context::kTarget, context::kEngine), registry};
        Virtual<uint8_t>(registry, 0x58, from, reinterpret_cast<void*>(addr::cSCLua_F880710),
                         static_cast<void*>(pair));
    };

    bool everything = kind == 1;
    if (kind == 3) {
        if (entry == Virtual<void*>(registry, 0x54)) { // the root: sc4game = {}, kept under the entry
            Virtual<void>(thread, 0xE0);
            Virtual<void>(thread, 0xCC, Global<const char*>(addr::cSCLua_RootTableName));
            Virtual<void>(thread, 0x40, int32_t{-2});
            Virtual<void>(thread, 0x104, kGlobalsIndex);
            Virtual<void>(thread, 0xD8, entry);
            Virtual<void>(thread, 0x40, int32_t{-2});
            Virtual<void>(thread, 0x104, kRegistryIndex);
        } else if (pushParentAndKey()) {
            if (Virtual<uint8_t>(registry, 0x34, entry)) { // a node: parent[key] = {_e = entry}
                Virtual<void>(thread, 0xE0);
                Virtual<void>(thread, 0xCC, Global<const char*>(addr::cSCLua_EntryFieldName));
                Virtual<void>(thread, 0xD8, entry);
                Virtual<void>(thread, 0x104, int32_t{-3});
                Virtual<void>(thread, 0xD8, entry);
                Virtual<void>(thread, 0x40, int32_t{-2});
                Virtual<void>(thread, 0x104, kRegistryIndex);
                Virtual<void>(thread, 0x104, int32_t{-3});
            } else { // a value: parent[key] = a closure over (registry, entry, this context)
                const std::uintptr_t closure = Virtual<uint8_t>(registry, 0x4C, entry, uint32_t{1})
                                                   ? addr::cSCLua_F881260
                                                   : addr::cSCLua_F880F50;
                Virtual<void>(thread, 0xD8, static_cast<void*>(registry));
                Virtual<void>(thread, 0xD8, entry);
                Virtual<void>(thread, 0xD8, static_cast<void*>(self - context::kTarget));
                Virtual<void>(thread, 0xD0, reinterpret_cast<void*>(closure), int32_t{3});
                Virtual<void>(thread, 0x104, int32_t{-3});
            }
        }
    } else if (kind == 2) {
        if (entry == Virtual<void*>(registry, 0x54)) {
            everything = true;
        } else if (pushParentAndKey()) {
            Virtual<void>(thread, 0xC0);
            Virtual<void>(thread, 0x104, int32_t{-3});
            if (Virtual<uint8_t>(registry, 0x34, entry))
                forgetBelow(entry);
        }
    }
    if (everything) {
        Virtual<void>(thread, 0xCC, Global<const char*>(addr::cSCLua_RootTableName));
        Virtual<void>(thread, 0xC0);
        Virtual<void>(thread, 0x104, kGlobalsIndex);
        forgetBelow(Virtual<void*>(registry, 0x54));
        // SetGCThreshold(0) on the engine part.
        Virtual<void>(self - context::kTarget + context::kEngine, 0x30, int32_t{0});
    }
    return Virtual<uint32_t>(thread, 0x3C, top);
}

// ---------------------------------------------------------------------------
// The vector of records (begin, end, end of storage at +0, +4, +8).

int32_t Count(const char* first, const char* last)
{
    return static_cast<int32_t>(last - first) / static_cast<int32_t>(record::kSize);
}

// The original's cast of a string to its buffer: null only for a null
// string.
char* Buffer(char* string)
{
    return string ? string + 4 : nullptr;
}

// The game's 0x008B5E70. `this` is the record: two empty strings; the id is
// not set. Also used by code at 0x0043B738 and 0x0043B8D2.
char* __fastcall RecordConstruct(char* self, void*)
{
    for (const std::size_t string : {record::kName, record::kText}) {
        At<char*>(self, string + 0x4) = nullptr;
        At<char*>(self, string + 0x8) = nullptr;
        At<char*>(self, string + 0xC) = nullptr;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        At<char*>(self, string + 0x4) = buffer;
        At<char*>(self, string + 0x8) = buffer;
        At<char*>(self, string + 0xC) = buffer + 8;
        *buffer = 0;
        At<std::uintptr_t>(self, string) = addr::SC4String_Vtable;
        At<uint32_t>(self, string + 0x10) = 0;
    }
    return self;
}

// The game's 0x008B5ED0 (cdecl): the end of the records of a range.
void __cdecl RecordsDestroyRange(char* first, char* last, void*)
{
    for (; first != last; first += record::kSize) {
        At<std::uintptr_t>(first, record::kText) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(first, record::kText + 4))
            Free(buffer);
        At<std::uintptr_t>(first, record::kName) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(first, record::kName + 4))
            Free(buffer);
    }
}

// The game's 0x008B5F20. `this` is raw memory: a copy of `source` made
// there.
char* __fastcall RecordCopy(char* self, void*, char* source)
{
    void* tag = source;
    At<uint32_t>(self, record::kId) = At<uint32_t>(source, record::kId);
    This<void>(addr::SC4String_CopyBuffer, self + record::kName + 4, Buffer(source + record::kName), uint32_t{0},
               uint32_t{0xFFFFFFFF}, &tag);
    At<std::uintptr_t>(self, record::kName) = addr::SC4String_Vtable;
    At<uint32_t>(self, record::kName + 0x10) = 0;
    This<void>(addr::SC4String_CopyBuffer, self + record::kText + 4, Buffer(source + record::kText), uint32_t{0},
               uint32_t{0xFFFFFFFF}, &tag);
    At<std::uintptr_t>(self, record::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, record::kText + 0x10) = 0;
    return self;
}

// A record assigned to another: the id, and each string's characters unless
// the buffer is the same.
void RecordAssign(char* to, char* from, void* tag)
{
    At<uint32_t>(to, record::kId) = At<uint32_t>(from, record::kId);
    for (const std::size_t string : {record::kName, record::kText}) {
        char* const buffer = Buffer(from + string);
        if (buffer != to + string + 4)
            This<void>(addr::SC4String_AssignRange, to + string + 4, At<char*>(buffer, 0), At<char*>(buffer, 4), tag);
    }
}

// The game's 0x008B5F90 (cdecl): copies of [first, last) made in raw memory
// at `to`; the end of the copies.
char* __cdecl RecordsUninitializedCopy(char* first, char* last, char* to, void*)
{
    for (; first != last; first += record::kSize, to += record::kSize) {
        if (to)
            RecordCopy(to, nullptr, first);
    }
    return to;
}

// The game's 0x008B5FC0 (cdecl): `count` copies of `value` made in raw
// memory at `to`; the end of the copies.
char* __cdecl RecordsUninitializedFill(char* to, uint32_t count, char* value)
{
    for (; count != 0; --count, to += record::kSize) {
        if (to)
            RecordCopy(to, nullptr, value);
    }
    return to;
}

// The game's 0x008B5FF0. `this` is the vector: `count` copies of `value`
// put in at `position` where the storage is too small: new storage of the
// old size plus the greater of the old size and `count`.
void __fastcall RecordsInsertOverflow(char* self, void*, char* position, char* value, void*, uint32_t count,
                                      uint8_t atEnd)
{
    void* tag = nullptr;
    const uint32_t size = static_cast<uint32_t>(Count(At<char*>(self, 0), At<char*>(self, 4)));
    const uint32_t capacity = size + (size < count ? count : size);
    char* const storage =
        capacity ? Cdecl<char*>(addr::SC4_stl_allocate, capacity * static_cast<uint32_t>(record::kSize)) : nullptr;
    char* to = storage;
    for (char* from = At<char*>(self, 0); from != position; from += record::kSize, to += record::kSize) {
        if (to)
            RecordCopy(to, nullptr, from);
    }
    for (uint32_t left = count; left != 0; --left, to += record::kSize) {
        if (to)
            RecordCopy(to, nullptr, value);
    }
    if (!atEnd)
        to = RecordsUninitializedCopy(position, At<char*>(self, 4), to, &tag);
    RecordsDestroyRange(At<char*>(self, 0), At<char*>(self, 4), &tag);
    if (void* const old = At<void*>(self, 0))
        Free(old);
    At<char*>(self, 4) = to;
    At<char*>(self, 8) = storage + capacity * static_cast<uint32_t>(record::kSize);
    At<char*>(self, 0) = storage;
}

// The game's 0x008B6110 (cdecl): [first, last) assigned to the records that
// end at `toEnd`, last one first; the start of the assigned records.
char* __cdecl RecordsCopyBackward(char* first, char* last, char* toEnd, void*, int32_t)
{
    int32_t count = Count(first, last);
    if (count <= 0)
        return toEnd;
    do {
        last -= record::kSize;
        toEnd -= record::kSize;
        RecordAssign(toEnd, last, &count);
    } while (--count != 0);
    return toEnd;
}

// The game's 0x008B6340 (cdecl): `value` assigned to every record of
// [first, last). EAX is `last` afterwards.
char* __cdecl RecordsFill(char* first, char* last, char* value)
{
    void* tag = nullptr;
    for (; first != last; first += record::kSize)
        RecordAssign(first, value, &tag);
    return last;
}

// The game's 0x008B63D0 (cdecl): [first, last) assigned to the records at
// `to`, first one first; the end of the assigned records.
char* __cdecl RecordsCopyRange(char* first, char* last, char* to, void*, int32_t)
{
    int32_t count = Count(first, last);
    if (count <= 0)
        return to;
    do {
        RecordAssign(to, first, &count);
        first += record::kSize;
        to += record::kSize;
    } while (--count != 0);
    return to;
}

// The game's 0x008B6550. `this` is the vector: `count` copies of `value`
// put in at `position`.
void __fastcall RecordsInsertFill(char* self, void*, char* position, uint32_t count, char* value)
{
    if (count == 0)
        return;
    void* tag = nullptr;
    if (static_cast<uint32_t>(Count(At<char*>(self, 4), At<char*>(self, 8))) < count) {
        RecordsInsertOverflow(self, nullptr, position, value, &tag, count, 0);
        return;
    }

    alignas(4) char copy[record::kSize];
    RecordCopy(copy, nullptr, value);
    char* const oldEnd = At<char*>(self, 4);
    const uint32_t after = static_cast<uint32_t>(Count(position, At<char*>(self, 4)));
    if (after > count) {
        const std::ptrdiff_t bytes = static_cast<std::ptrdiff_t>(count) * record::kSize;
        char* const tail = oldEnd - bytes;
        RecordsUninitializedCopy(tail, oldEnd, oldEnd, &tag);
        At<char*>(self, 4) += bytes;
        RecordsCopyBackward(position, tail, oldEnd, &tag, 0);
        RecordsFill(position, position + bytes, copy);
    } else {
        const uint32_t extra = count - after;
        RecordsUninitializedFill(oldEnd, extra, copy);
        At<char*>(self, 4) += static_cast<std::ptrdiff_t>(extra) * record::kSize;
        RecordsUninitializedCopy(position, oldEnd, At<char*>(self, 4), &tag);
        At<char*>(self, 4) += static_cast<std::ptrdiff_t>(after) * record::kSize;
        RecordsFill(position, oldEnd, copy);
    }
    if (void* const buffer = At<void*>(copy, record::kText + 4))
        Free(buffer);
    if (void* const buffer = At<void*>(copy, record::kName + 4))
        Free(buffer);
}

// The game's 0x008B62A0. `this` is the vector: constructed with `count`
// empty records.
char* __fastcall RecordsConstruct(char* self, void*, uint32_t count)
{
    At<char*>(self, 0) = nullptr;
    At<char*>(self, 4) = nullptr;
    At<char*>(self, 8) = nullptr;
    char* const storage =
        count ? Cdecl<char*>(addr::SC4_stl_allocate, count * static_cast<uint32_t>(record::kSize)) : nullptr;
    At<char*>(self, 8) = storage + count * static_cast<uint32_t>(record::kSize);
    At<char*>(self, 0) = storage;
    At<char*>(self, 4) = storage;

    alignas(4) char empty[record::kSize];
    char* const made = RecordConstruct(empty, nullptr);
    char* to = At<char*>(self, 0);
    for (; count != 0; --count, to += record::kSize) {
        if (to)
            RecordCopy(to, nullptr, made);
    }
    At<char*>(self, 4) = to;

    void* buffer = At<void*>(empty, record::kText + 4);
    At<std::uintptr_t>(empty, record::kText) = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
    buffer = At<void*>(empty, record::kName + 4);
    At<std::uintptr_t>(empty, record::kName) = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
    return self;
}

// The game's 0x008B66A0. `this` is the vector: emptied, the storage kept.
void __fastcall RecordsClear(char* self, void*)
{
    uint8_t tag = 0;
    char* const end = At<char*>(self, 4);
    char* const newEnd = RecordsCopyRange(end, end, At<char*>(self, 0), &tag, 0);
    RecordsDestroyRange(newEnd, At<char*>(self, 4), &tag);
    At<char*>(self, 4) = newEnd;
}

// The game's 0x008B66E0. `this` is the vector: cut down to `count` records,
// or filled up to it with copies of `value`.
void __fastcall RecordsResize(char* self, void*, uint32_t count, char* value)
{
    char* const begin = At<char*>(self, 0);
    char* const end = At<char*>(self, 4);
    const uint32_t size = static_cast<uint32_t>(Count(begin, end));
    if (count >= size) {
        RecordsInsertFill(self, nullptr, end, count - size, value);
        return;
    }
    void* tag = nullptr;
    char* const newEnd =
        RecordsCopyRange(end, end, begin + static_cast<std::ptrdiff_t>(count) * record::kSize, &tag, 0);
    RecordsDestroyRange(newEnd, At<char*>(self, 4), &tag);
    At<char*>(self, 4) = newEnd;
}

// ---------------------------------------------------------------------------
// The native function table.

// The game's 0x008B59B0: slot 3 of the setup interface (+0xC), the table
// interface of the same object.
char* __fastcall SetupAsTable(char* self, void*)
{
    return Part(self, table::kSetupPart, table::kTablePart);
}

// The game's 0x008B59C0. `this` is the object. Where the object is null the
// base is asked after all, as in the original.
uint8_t __fastcall TableQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    *object = nullptr;
    if (iid == kTableIid || iid == kSetupIid) {
        char* const part = self ? self + (iid == kTableIid ? table::kTablePart : table::kSetupPart) : nullptr;
        *object = part;
        if (part) {
            Virtual<uint32_t>(self, 0x4);
            return 1;
        }
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, object);
}

// The game's 0x008B6280 and 0x008B6290: QueryInterface of the table
// interface (+8) and of the setup interface (+0xC).
uint8_t __fastcall TableQueryInterfaceOfTable(char* self, void*, uint32_t iid, void** object)
{
    return TableQueryInterface(self - table::kTablePart, nullptr, iid, object);
}

uint8_t __fastcall TableQueryInterfaceOfSetup(char* self, void*, uint32_t iid, void** object)
{
    return TableQueryInterface(self - table::kSetupPart, nullptr, iid, object);
}

// The game's 0x008B5A10. `this` is the id map: the node of an iterator
// (node, map; passed by value) taken out.
void __fastcall IdMapErase(char* self, void*, char* node, char* map)
{
    char* iterator[2] = {node, map};
    This<void>(addr::SC4Uint32HashMap_EraseAt, self, static_cast<void*>(iterator));
}

// The game's 0x008B5A40: slot 6 of the table interface (+8),
// RemoveFunction. Only the id is used. The function's record goes back on
// the free list, its id out of the map, and its name in the script table is
// set to nil. The record keeps its id and strings.
uint8_t __fastcall TableRemoveFunction(char* self, void*, uint32_t id, uint32_t, uint32_t)
{
    char* const object = self - table::kTablePart;
    char* const buckets = At<char*>(object, table::kBuckets);
    const uint32_t bucketCount =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(object, table::kBucketsEnd) - buckets) >> 2);
    char* node = At<char*>(buckets, (id % bucketCount) * 4);
    while (node && At<uint32_t>(node, 4) != id)
        node = At<char*>(node, 0);
    if (!node)
        return 0;
    int32_t index = At<int32_t>(node, 8);
    char* const begin = At<char*>(object, table::kRecords);
    if (index >= Count(begin, At<char*>(object, table::kRecordsEnd)) || index < 0)
        return 0;
    char* const found = begin + index * record::kSize;
    if (At<uint32_t>(found, record::kId) != id)
        return 0;

    // push_back on the free list (the game's shared list code).
    This<void>(addr::cSC4TrafficNetworkMap_PushAutomaton, object + table::kFreeList, &index);
    IdMapErase(object + table::kById, nullptr, node, object + table::kById);

    void* thread = At<void*>(object, table::kContext);
    Virtual<void>(thread, 0xCC, Virtual<const char*>(object + table::kTableName, 0x14));
    Virtual<void>(At<void*>(object, table::kContext), 0xE8, kGlobalsIndex);
    if (Virtual<int32_t>(At<void*>(object, table::kContext), 0x5C, int32_t{-1}) == kLuaTable) {
        thread = At<void*>(object, table::kContext);
        Virtual<void>(thread, 0xCC, Virtual<const char*>(found + record::kName, 0x14));
        Virtual<void>(At<void*>(object, table::kContext), 0xC0);
        Virtual<void>(At<void*>(object, table::kContext), 0x104, int32_t{-3});
    }
    Virtual<void>(At<void*>(object, table::kContext), 0x4C, int32_t{1});
    return 1;
}

// The game's 0x008B5B70 (cdecl, a Lua closure): what a script calls for a
// registered native. Its upvalues are the record's index, the table and the
// table's thread. The arguments go into the input parameter set (booleans,
// numbers and strings; anything else as 0.0), the owner runs the function
// by its id, and the results are the owner's number followed by every
// parameter of the output set but the status. A call that cannot be made
// gives the one number at 0x00A80AC8.
int32_t __cdecl TableDispatch(char* state)
{
    char* const thread = *reinterpret_cast<char**>(state - 4);
    char* const object = Upvalue(thread, 2);
    char* const home = Upvalue(thread, 3);
    const auto fail = [thread] {
        Virtual<void>(thread, 0xC4, Global<double>(addr::SC4_DoubleTwo));
        return int32_t{1};
    };
    if (!object || !At<void*>(object, table::kOwner) || home != At<char*>(object, table::kContext))
        return fail();
    const int32_t index = Ftol(Virtual<double>(thread, 0xA4, Virtual<int32_t>(thread, 0x148, int32_t{1})));
    char* const begin = At<char*>(object, table::kRecords);
    if (index >= Count(begin, At<char*>(object, table::kRecordsEnd)) || index < 0)
        return fail();
    char* const called = begin + index * record::kSize;

    Virtual<void>(At<void*>(object, table::kInput), 0x10, uint32_t{0}); // SetParameterCount
    Virtual<void>(At<void*>(object, table::kOutput), 0x10, uint32_t{0});
    const int32_t top = Virtual<int32_t>(thread, 0x38);
    for (int32_t argument = 1; argument <= top; ++argument) {
        ScriptValue value;
        This<void*>(addr::SC4ScriptNumber_Construct, &value);
        switch (Virtual<int32_t>(thread, 0x5C, argument)) {
        case kLuaBoolean:
            This<void>(addr::SC4ScriptNumber_SetBool, &value, Virtual<uint8_t>(thread, 0xA8, argument));
            break;
        case kLuaNumber:
            This<void>(addr::SC4ScriptNumber_SetDouble, &value, Virtual<double>(thread, 0xA4, argument));
            break;
        case kLuaString:
            SetText(&value, Virtual<const char*>(thread, 0xAC, argument));
            break;
        default:
            This<void>(addr::SC4ScriptNumber_SetDouble, &value, Global<double>(addr::SC4_DoubleZero));
            break;
        }
        Virtual<void>(At<void*>(object, table::kInput), 0x1C, static_cast<uint32_t>(argument - 1), &value);
        This<void>(addr::SC4ScriptNumber_Destroy, &value);
    }

    const int32_t status = Virtual<int32_t>(At<void*>(object, table::kOwner), 0x50, At<uint32_t>(called, record::kId),
                                            At<void*>(object, table::kInput), At<void*>(object, table::kOutput));
    Virtual<void>(thread, 0xC4, static_cast<double>(status));

    int32_t results = 1;
    GameString text;
    This<void*>(addr::SC4String_SC4String, &text);
    uint32_t id = Virtual<uint32_t>(At<void*>(object, table::kOutput), 0x34); // GetFirstParameterID
    while (id != 0xFFFFFFFF) {
        if (id != kStatusParameter) {
            void* const item = Virtual<void*>(At<void*>(object, table::kOutput), 0x18, id); // GetParameter
            if (!item) {
                Virtual<void>(thread, 0xC0);
            } else {
                const int32_t type = Virtual<int32_t>(item, 0xC);
                if (type == 1) {
                    Virtual<void>(thread, 0xD4, Virtual<uint8_t>(item, 0x24));
                } else if (type > 1 && type <= 14) {
                    Virtual<void>(thread, 0xC4, Virtual<double>(item, 0x9C));
                } else {
                    Virtual<uint8_t>(item, 0xDC, &text);
                    Virtual<void>(thread, 0xCC, This<const char*>(addr::cRZBaseString_Data, &text));
                }
            }
            ++results;
        }
        id = Virtual<uint32_t>(At<void*>(object, table::kOutput), 0x38, id); // GetNextParameterID
    }
    DropString(text);
    if (results == 0)
        return fail();
    return results;
}

// The game's 0x008B61C0: the destructor. `this` is the object.
void __fastcall TableDestroy(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SCLuaFunctionTable_Vtable;
    At<std::uintptr_t>(self, table::kTablePart) = addr::SCLuaFunctionTable_TableVtable;
    At<std::uintptr_t>(self, table::kSetupPart) = addr::SCLuaFunctionTable_SetupVtable;
    At<std::uintptr_t>(self, table::kTableName) = addr::SC4String_Vtable;
    if (void* const buffer = At<void*>(self, table::kTableName + 4))
        Free(buffer);
    for (const std::size_t held : {table::kOwner, table::kOutput, table::kInput}) {
        if (void* const reference = At<void*>(self, held))
            Virtual<uint32_t>(reference, 0x8);
    }
    This<void>(addr::SC4Uint32HashSet_Clear, self + table::kById);
    if (void* const buckets = At<void*>(self, table::kBuckets))
        Free(buckets);
    This<void>(addr::SC4PairList_Destroy, self + table::kFreeList);
    uint8_t tag = 0;
    RecordsDestroyRange(At<char*>(self, table::kRecords), At<char*>(self, table::kRecordsEnd), &tag);
    if (void* const storage = At<void*>(self, table::kRecords))
        Free(storage);
    if (void* const thread = At<void*>(self, table::kContext))
        Virtual<uint32_t>(thread, 0x8);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// The game's 0x008B6530: the deleting destructor.
char* __fastcall TableDelete(char* self, void*, uint32_t flags)
{
    TableDestroy(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x008B6480: the constructor. `this` is the object. The
// abstract interfaces' vtables are written first, as the original does. The
// vector starts with 500 records, the id map with room for 100.
char* __fastcall TableConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, table::kTablePart) = addr::SCLuaFunctionTable_AbstractTableVtable;
    At<std::uintptr_t>(self, table::kSetupPart) = addr::SCLuaFunctionTable_AbstractSetupVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SCLuaFunctionTable_Vtable;
    At<std::uintptr_t>(self, table::kTablePart) = addr::SCLuaFunctionTable_TableVtable;
    At<std::uintptr_t>(self, table::kSetupPart) = addr::SCLuaFunctionTable_SetupVtable;
    At<void*>(self, table::kContext) = nullptr;
    RecordsConstruct(self + table::kRecords, nullptr, 500);

    At<char*>(self, table::kFreeList) = nullptr;
    char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0xC});
    At<char*>(node, 0x0) = node;
    At<char*>(node, 0x4) = node;
    At<char*>(self, table::kFreeList) = node;

    char* const map = self + table::kById;
    At<uint32_t>(map, 0x4) = 0;
    At<uint32_t>(map, 0x8) = 0;
    At<uint32_t>(map, 0xC) = 0;
    At<uint32_t>(map, 0x10) = 0;
    This<void>(addr::SC4Uint32HashSet_Construct, map, uint32_t{100});

    At<void*>(self, table::kInput) = nullptr;
    At<void*>(self, table::kOutput) = nullptr;
    At<void*>(self, table::kOwner) = nullptr;
    At<char*>(self, table::kTableName + 0x4) = nullptr;
    At<char*>(self, table::kTableName + 0x8) = nullptr;
    At<char*>(self, table::kTableName + 0xC) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, table::kTableName + 0x4) = buffer;
    At<char*>(self, table::kTableName + 0x8) = buffer;
    At<char*>(self, table::kTableName + 0xC) = buffer + 8;
    *buffer = 0;
    At<uint32_t>(self, table::kTableName + 0x10) = 0;
    At<std::uintptr_t>(self, table::kTableName) = addr::SC4String_Vtable;
    return self;
}

// A new parameter set (0x28 bytes), or null.
char* NewParameterSet()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
    return memory ? This<char*>(addr::SC4ScriptParameterSet_Construct, memory) : nullptr;
}

// The game's 0x008B6750: slot 4 of the setup interface (+0xC). (owner, the
// script engine, the name of the script table as a string): the owner is
// told of the table, the script table is made if the global of that name is
// nil, and the table starts empty with two new parameter sets. Fails for an
// empty name, a missing argument, an owner that refuses, or a global of
// that name that is neither nil nor a table (the owner is then told the
// table is gone).
uint8_t __fastcall TableSetup(char* self, void*, void* owner, void* engine, void* name)
{
    char* const object = self - table::kSetupPart;
    if (!Virtual<uint32_t>(name, 0x1C) || !engine || !owner) // Strlen
        return 0;
    if (!Virtual<uint8_t>(owner, 0x3C, Part(self, table::kSetupPart, table::kTablePart)))
        return 0;

    char* const thread = Virtual<char*>(engine, 0xC); // AsIGZLua5Thread
    const char* const characters = Virtual<const char*>(name, 0x14);
    Virtual<void>(thread, 0xCC, characters);
    Virtual<void>(thread, 0xE8, kGlobalsIndex);
    const int32_t type = Virtual<int32_t>(thread, 0x5C, int32_t{-1});
    Virtual<void>(thread, 0x4C, int32_t{1});
    if (type != kLuaTable) {
        if (type != kLuaNil) {
            Virtual<uint8_t>(owner, 0x40, Part(self, table::kSetupPart, table::kTablePart));
            return 0;
        }
        Virtual<void>(thread, 0xCC, characters);
        Virtual<void>(thread, 0xE0);
        Virtual<void>(thread, 0x104, kGlobalsIndex);
    }

    This<void>(addr::SC4RefPtr_Assign, object + table::kContext, static_cast<void*>(thread));
    This<void>(addr::SC4RefPtr_Assign, object + table::kOwner, owner);
    Virtual<uint32_t>(object + table::kTableName, 0x34, name); // Copy
    This<void>(addr::SC4RefPtr_Assign, object + table::kInput, static_cast<void*>(NewParameterSet()));
    This<void>(addr::SC4RefPtr_Assign, object + table::kOutput, static_cast<void*>(NewParameterSet()));
    RecordsClear(object + table::kRecords, nullptr);
    This<void>(addr::SC4StlList_Clear, object + table::kFreeList);
    This<void>(addr::SC4Uint32HashSet_Clear, object + table::kById);
    return 1;
}

bool InstallRest1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSCLua_F881260, reinterpret_cast<void*>(&RegistryValueCall)},
        {addr::cSCLua_F881650, reinterpret_cast<void*>(&OnRegistryNotification)},
        {addr::cSCLua_F8B59B0, reinterpret_cast<void*>(&SetupAsTable)},
        {addr::cSCLua_F8B59C0, reinterpret_cast<void*>(&TableQueryInterface)},
        {addr::cSCLua_F8B5A10, reinterpret_cast<void*>(&IdMapErase)},
        {addr::cSCLua_F8B5A40, reinterpret_cast<void*>(&TableRemoveFunction)},
        {addr::SCLuaFunctionTable_Dispatch, reinterpret_cast<void*>(&TableDispatch)},
        {addr::SCLuaRecord_Construct, reinterpret_cast<void*>(&RecordConstruct)},
        {addr::SCLuaRecords_DestroyRange, reinterpret_cast<void*>(&RecordsDestroyRange)},
        {addr::cSCLua_F8B5F20, reinterpret_cast<void*>(&RecordCopy)},
        {addr::cSCLua_F8B5F90, reinterpret_cast<void*>(&RecordsUninitializedCopy)},
        {addr::cSCLua_F8B5FC0, reinterpret_cast<void*>(&RecordsUninitializedFill)},
        {addr::cSCLua_F8B5FF0, reinterpret_cast<void*>(&RecordsInsertOverflow)},
        {addr::cSCLua_F8B6110, reinterpret_cast<void*>(&RecordsCopyBackward)},
        {addr::cSCLua_F8B61C0, reinterpret_cast<void*>(&TableDestroy)},
        {addr::cSCLua_F8B6280, reinterpret_cast<void*>(&TableQueryInterfaceOfTable)},
        {addr::cSCLua_F8B6290, reinterpret_cast<void*>(&TableQueryInterfaceOfSetup)},
        {addr::cSCLua_F8B62A0, reinterpret_cast<void*>(&RecordsConstruct)},
        {addr::cSCLua_F8B6340, reinterpret_cast<void*>(&RecordsFill)},
        {addr::SCLuaRecords_CopyRange, reinterpret_cast<void*>(&RecordsCopyRange)},
        {addr::SC4ScriptClass2BD13FB4_Construct, reinterpret_cast<void*>(&TableConstruct)},
        {addr::cSCLua_F8B6530, reinterpret_cast<void*>(&TableDelete)},
        {addr::cSCLua_F8B6550, reinterpret_cast<void*>(&RecordsInsertFill)},
        {addr::cSCLua_F8B66A0, reinterpret_cast<void*>(&RecordsClear)},
        {addr::SCLuaRecords_Resize, reinterpret_cast<void*>(&RecordsResize)},
        {addr::cSCLua_F8B6750, reinterpret_cast<void*>(&TableSetup)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static void* originals[count] = {};
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_rest1Registered = registry::Register({
    "scripts.context.rest1",
    "scripts/context",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SCLuaFunctionTable_Dispatch,
    "Our script classes, rest 1: the data registry's call closure and notifications, the native function table "
    "(docs/lua.md)",
    false,
    InstallRest1,
});

} // namespace
} // namespace opensc4::script_context
