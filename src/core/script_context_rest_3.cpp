// The game's tag library (0x008BE930 to 0x008BF270, cGZLua5Compat): Lua 4
// tags done with Lua 5 metatables. The thread's tag methods
// (script_context_3.cpp) run these as Lua C functions through a protected
// call, and the engine's Open registers seven of them as globals when asked
// for Lua 4 compatibility.
//
// How a tag is kept: the registry holds one table under the number
// 1262289877; in it every tag (a number) has a table, made when first asked
// for, whose entry under the number 187693250 is the tag itself. A value has
// a tag when that table is its metatable. Tag methods are metatable entries:
// the Lua 4 event names map to Lua 5's "__index", "__add" and so on.
//
// Replacement, off by default:
//
//   scripts.context.rest3: the 13 functions below.
//
// Three of them take arguments in registers (the state in ESI or EAX, the
// event name in EAX) and are hooked through small thunks. Lua itself and
// lauxlib (0x008BE900 is luaL_optnumber) stay the game's (docs/lua.md).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::script_context {
namespace {

constexpr int32_t kRegistryIndex = -10000;
constexpr int32_t kGlobalsIndex = -10001;
constexpr int32_t kTypeNil = 0;
constexpr int32_t kTypeString = 4;
constexpr int32_t kTypeTable = 5;

// The registry's key of the table of tags (the double at 0x00ACA788) and a
// tag table's key of its own tag (0x00ACA780).
constexpr double kTagsKey = 1262289877.0;
constexpr double kTagKey = 187693250.0;

// Slots of the engine (cIGZLua5) and of the thread interface.
constexpr std::size_t kAsThread = 0xC;
constexpr std::size_t kDoString = 0x120;
constexpr std::size_t kRegister = 0x144;

// Entries of Lua's table of event names (luaT_eventname, 0x00B16000).
namespace event {
constexpr std::size_t kIndex = 0;
constexpr std::size_t kNewIndex = 1;
constexpr std::size_t kGc = 2;
constexpr std::size_t kAdd = 5;
constexpr std::size_t kSub = 6;
constexpr std::size_t kMul = 7;
constexpr std::size_t kDiv = 8;
constexpr std::size_t kPow = 9;
constexpr std::size_t kUnm = 10;
constexpr std::size_t kLe = 12;
constexpr std::size_t kConcat = 13;
constexpr std::size_t kCall = 14;
} // namespace event

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The x87 precision switch around every Lua call: 24-bit precision (setting
// 0) is raised to 53 bits and put back afterwards.
struct Precision {
    int32_t saved;
    bool raised;
};

Precision RaisePrecision()
{
    Precision state;
    state.saved = Cdecl<int32_t>(addr::SC4Precision_Get);
    state.raised = state.saved == 0;
    if (state.raised)
        Cdecl<int32_t>(addr::SC4Precision_Set, int32_t{1});
    return state;
}

void RestorePrecision(const Precision& state)
{
    if (state.raised)
        Cdecl<int32_t>(addr::SC4Precision_Set, state.saved);
}

// Lua's API, the game's.
int32_t GetTop(void* L)
{
    return Cdecl<int32_t>(addr::Lua_GetTop, L);
}

void SetTop(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_SetTop, L, index);
}

void Replace(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_Replace, L, index);
}

void PushValue(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_PushValue, L, index);
}

int32_t Type(void* L, int32_t index)
{
    return Cdecl<int32_t>(addr::Lua_Type, L, index);
}

double ToNumber(void* L, int32_t index)
{
    return Cdecl<double>(addr::Lua_ToNumber, L, index);
}

const char* ToString(void* L, int32_t index)
{
    return Cdecl<const char*>(addr::Lua_ToString, L, index);
}

void PushNil(void* L)
{
    Cdecl<void>(addr::Lua_PushNil, L);
}

void PushNumber(void* L, double value)
{
    Cdecl<void>(addr::Lua_PushNumber, L, value);
}

void PushString(void* L, const char* text)
{
    Cdecl<void>(addr::Lua_PushString, L, text);
}

void RawGet(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_RawGet, L, index);
}

void RawSet(void* L, int32_t index)
{
    Cdecl<void>(addr::Lua_RawSet, L, index);
}

void NewTable(void* L)
{
    Cdecl<void>(addr::Lua_NewTable, L);
}

const char* EventNameAt(std::size_t entry)
{
    return Global<const char*>(addr::Lua_EventNames + entry * 4);
}

// The game's 0x008BE930, the Lua function `newtag`: the next number of a
// counter, alone on the stack.
int32_t __cdecl NewTag(void* L)
{
    const Precision precision = RaisePrecision();
    SetTop(L, 0);
    int32_t& counter = Global<int32_t>(addr::cGZLua5Compat_TagCounter);
    ++counter;
    PushNumber(L, static_cast<double>(counter));
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BE990, the Lua function `tag`: the tag kept in the
// metatable of the first argument, -1 when it has none.
int32_t __cdecl Tag(void* L)
{
    const Precision precision = RaisePrecision();
    SetTop(L, 1);
    bool found = false;
    if (Cdecl<int32_t>(addr::Lua_GetMetaTable, L, int32_t{-1})) {
        PushNumber(L, kTagKey);
        RawGet(L, -2);
        Replace(L, -2);
        found = Cdecl<int32_t>(addr::Lua_IsNumber, L, int32_t{-1}) != 0;
    }
    if (!found) {
        SetTop(L, 0);
        PushNumber(L, -1.0);
    }
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BEA30, the Lua function `globals`: the table of globals.
int32_t __cdecl Globals(void* L)
{
    const Precision precision = RaisePrecision();
    PushValue(L, kGlobalsIndex);
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BEA80 (the state in ESI): pushes the registry's table of
// tags, made when it is not there yet.
void __cdecl PushTags(void* L)
{
    const Precision precision = RaisePrecision();
    PushNumber(L, kTagsKey);
    RawGet(L, kRegistryIndex);
    if (Type(L, -1) != kTypeTable) {
        SetTop(L, -2);
        NewTable(L);
        PushNumber(L, kTagsKey);
        PushValue(L, -2);
        RawSet(L, kRegistryIndex);
    }
    RestorePrecision(precision);
}

__declspec(naked) void PushTagsThunk()
{
    __asm push esi
    __asm call PushTags
    __asm add esp, 4
    __asm ret
}

// The game's 0x008BEB10 (the name in EAX): the Lua 5 metatable key of a
// Lua 4 event name, null for a null or unknown name. Lua 4's "eq", "lt" and
// "getglobal"/"setglobal" have no entry.
const char* __cdecl EventName(const char* name)
{
    if (!name)
        return nullptr;
    if (std::strcmp(name, "index") == 0 || std::strcmp(name, "gettable") == 0)
        return EventNameAt(event::kIndex);
    if (std::strcmp(name, "newindex") == 0 || std::strcmp(name, "settable") == 0)
        return EventNameAt(event::kNewIndex);
    if (std::strcmp(name, "gc") == 0)
        return EventNameAt(event::kGc);
    if (std::strcmp(name, "add") == 0)
        return EventNameAt(event::kAdd);
    if (std::strcmp(name, "sub") == 0)
        return EventNameAt(event::kSub);
    if (std::strcmp(name, "mul") == 0)
        return EventNameAt(event::kMul);
    if (std::strcmp(name, "div") == 0)
        return EventNameAt(event::kDiv);
    if (std::strcmp(name, "pow") == 0)
        return EventNameAt(event::kPow);
    if (std::strcmp(name, "unm") == 0)
        return EventNameAt(event::kUnm);
    if (std::strcmp(name, "le") == 0)
        return EventNameAt(event::kLe);
    if (std::strcmp(name, "concat") == 0)
        return EventNameAt(event::kConcat);
    if (std::strcmp(name, "function") == 0)
        return EventNameAt(event::kCall);
    return nullptr;
}

__declspec(naked) void EventNameThunk()
{
    __asm push eax
    __asm call EventName
    __asm add esp, 4
    __asm ret
}

// The game's 0x008BED40 (the state in EAX, the tag on the stack, removed by
// the caller): pushes the table of a tag, made when it is not there yet.
void __cdecl PushTagTable(void* L, int32_t tag)
{
    const Precision precision = RaisePrecision();
    PushTags(L);
    const int32_t tags = GetTop(L);
    const double number = static_cast<double>(tag);
    PushNumber(L, number);
    RawGet(L, tags);
    if (Type(L, -1) != kTypeTable) {
        SetTop(L, -2);
        NewTable(L);
        PushNumber(L, number);
        PushValue(L, -2);
        RawSet(L, tags);
        PushNumber(L, kTagKey);
        PushNumber(L, number);
        RawSet(L, -3);
    }
    Replace(L, -2);
    RestorePrecision(precision);
}

__declspec(naked) void PushTagTableThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push eax
    __asm call PushTagTable
    __asm add esp, 8
    __asm ret
}

// The game's 0x008BEC80, the Lua function `gettagmethod` (tag, event): the
// tag table's entry for the event; the nil itself for a nil event, nil when
// the tag has no table or the event is unknown.
int32_t __cdecl GetTagMethod(void* L)
{
    const Precision precision = RaisePrecision();
    SetTop(L, 2);
    if (Type(L, -1) != kTypeNil) {
        const char* const name = ToString(L, -1);
        const int32_t tag = Ftol(ToNumber(L, -2));
        PushTags(L);
        PushNumber(L, static_cast<double>(tag));
        RawGet(L, -2);
        const char* key = nullptr;
        if (Type(L, -1) == kTypeTable)
            key = EventName(name);
        if (key) {
            PushString(L, key);
            RawGet(L, -2);
        } else {
            PushNil(L);
        }
    }
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BEE20, the Lua function `settag` (value, tag): gives the
// value the tag's table as its metatable and returns the value.
int32_t __cdecl SetTagFunction(void* L)
{
    const Precision precision = RaisePrecision();
    SetTop(L, 2);
    const int32_t tag = Ftol(ToNumber(L, -1));
    SetTop(L, -2);
    PushTagTable(L, tag);
    Cdecl<int32_t>(addr::Lua_SetMetaTable, L, int32_t{-2});
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BEE90: `settag` on the value on top of the stack, in a
// protected call that drops the result and any error.
void __cdecl SetTag(void* L, int32_t tag)
{
    const Precision precision = RaisePrecision();
    Cdecl<void>(addr::Lua_PushCClosure, L, addr::cSCLua_F8BEE20, int32_t{0});
    PushValue(L, -2);
    PushNumber(L, static_cast<double>(tag));
    Cdecl<int32_t>(addr::Lua_PCall, L, int32_t{2}, int32_t{0}, int32_t{0});
    RestorePrecision(precision);
}

// The game's 0x008BEF00, the Lua function `settagmethod` (tag, event,
// function): sets the tag table's entry for the event and returns the old
// one, which it parks in a registry reference meanwhile. An unknown event
// gives a null key, pushed as nil, and the store then raises Lua's error for
// a nil index.
int32_t __cdecl SetTagMethod(void* L)
{
    const Precision precision = RaisePrecision();
    SetTop(L, 3);
    PushTagTable(L, Ftol(ToNumber(L, -3)));
    const char* const key = EventName(ToString(L, -3));
    if (key) {
        PushString(L, key);
        RawGet(L, -2);
    } else {
        PushNil(L);
    }
    const int32_t old = Cdecl<int32_t>(addr::LuaL_Ref, L, kRegistryIndex);
    PushString(L, key);
    PushValue(L, -3);
    RawSet(L, -3);
    SetTop(L, 0);
    Cdecl<void>(addr::Lua_RawGetI, L, kRegistryIndex, old);
    Cdecl<void>(addr::LuaL_Unref, L, kRegistryIndex, old);
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BEFE0, the Lua function `copytagmethods` (to, from):
// copies every entry with a string key from one tag's table to the other's
// and returns `to`. Both type tests look at the top of the stack, the table
// of `to`; the table of `from` is not tested.
int32_t __cdecl CopyTagMethods(void* L)
{
    const Precision precision = RaisePrecision();
    SetTop(L, 2);
    const int32_t to = Ftol(ToNumber(L, -2));
    const int32_t from = Ftol(ToNumber(L, -1));
    PushTagTable(L, from);
    const int32_t source = GetTop(L);
    PushTagTable(L, to);
    const int32_t target = GetTop(L);
    if (Type(L, -1) == kTypeTable && Type(L, -1) == kTypeTable) {
        PushNil(L);
        while (Cdecl<int32_t>(addr::Lua_Next, L, source)) {
            if (Type(L, -2) == kTypeString) {
                PushValue(L, -2);
                PushValue(L, -2);
                RawSet(L, target);
            }
            SetTop(L, -2);
        }
    }
    SetTop(L, 0);
    PushNumber(L, static_cast<double>(to));
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008BF100: pushes a user data holding the pointer and gives
// it the tag.
void __cdecl PushUserTag(void* L, void* data, int32_t tag)
{
    const Precision precision = RaisePrecision();
    Cdecl<void*>(addr::Lua_NewUserData, L, uint32_t{4});
    *Cdecl<void**>(addr::Lua_ToUserData, L, int32_t{-1}) = data;
    SetTag(L, tag);
    RestorePrecision(precision);
}

// The game's 0x008BF160: runs the compatibility script (the text whose
// address is at 0x00B15FFC) on the engine's main thread and registers the
// tag functions as globals. A null thread is tested before the count is
// taken but used all the same.
uint8_t __cdecl Open(void* lua)
{
    const Precision precision = RaisePrecision();
    void* const thread = Virtual<void*>(lua, kAsThread);
    if (thread)
        Virtual<uint32_t>(thread, 0x4);
    if (Virtual<int32_t>(thread, kDoString, Global<const char*>(addr::cGZLua5Compat_Script)) != 0) {
        Virtual<uint32_t>(thread, 0x8);
        RestorePrecision(precision);
        return 0;
    }
    Virtual<uint32_t>(thread, kRegister, addr::cGZLua5Compat_Tag, "tag");
    Virtual<uint32_t>(thread, kRegister, addr::cSCLua_F8BEE20, "settag");
    Virtual<uint32_t>(thread, kRegister, addr::cGZLua5Compat_SetTagMethod, "settagmethod");
    Virtual<uint32_t>(thread, kRegister, addr::cGZLua5Compat_GetTagMethod, "gettagmethod");
    Virtual<uint32_t>(thread, kRegister, addr::cGZLua5Compat_NewTag, "newtag");
    Virtual<uint32_t>(thread, kRegister, addr::cGZLua5Compat_CopyTagMethods, "copytagmethods");
    Virtual<uint32_t>(thread, kRegister, addr::cSCLua_F8BEA30, "globals");
    Virtual<uint32_t>(thread, 0x8);
    RestorePrecision(precision);
    return 1;
}

bool InstallRest3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[13] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZLua5Compat_NewTag, reinterpret_cast<void*>(&NewTag)},
        {addr::cGZLua5Compat_Tag, reinterpret_cast<void*>(&Tag)},
        {addr::cSCLua_F8BEA30, reinterpret_cast<void*>(&Globals)},
        {addr::cSCLua_F8BEA80, reinterpret_cast<void*>(&PushTagsThunk)},
        {addr::cSCLua_F8BEB10, reinterpret_cast<void*>(&EventNameThunk)},
        {addr::cGZLua5Compat_GetTagMethod, reinterpret_cast<void*>(&GetTagMethod)},
        {addr::cSCLua_F8BED40, reinterpret_cast<void*>(&PushTagTableThunk)},
        {addr::cSCLua_F8BEE20, reinterpret_cast<void*>(&SetTagFunction)},
        {addr::cGZLua5Compat_SetTag, reinterpret_cast<void*>(&SetTag)},
        {addr::cGZLua5Compat_SetTagMethod, reinterpret_cast<void*>(&SetTagMethod)},
        {addr::cGZLua5Compat_CopyTagMethods, reinterpret_cast<void*>(&CopyTagMethods)},
        {addr::cGZLua5Compat_PushUserTag, reinterpret_cast<void*>(&PushUserTag)},
        {addr::cGZLua5Compat_Open, reinterpret_cast<void*>(&Open)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_rest3Registered = registry::Register({
    "scripts.context.rest3",
    "scripts/context",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZLua5Compat_Open,
    "Our tag library under the Lua thread (cGZLua5Compat): Lua 4 tags and tag methods kept in Lua 5 metatables "
    "(docs/lua.md)",
    false,
    InstallRest3,
});

} // namespace
} // namespace opensc4::script_context
