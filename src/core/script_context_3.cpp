// The script context, third part (0x008B8400 to 0x008B96F0): the two classes
// under the game's script context (cSCLua), both wrappers of Lua 5.0.
//
//   cGZLua5Thread (0x1C bytes): one Lua state. Main vtable 0x00AC8F54 at +0,
//     the cIGZLua5Thread interface (vtable 0x00AC8DD0, 97 slots) at +8, then
//     the thread's ID (+0xC), the Lua state (+0x10), the owning cIGZLua5
//     (+0x14) and a user pointer (+0x18).
//   cGZLua5 (0x40 bytes), derived from it: the main state. Main vtable
//     0x00AC9144, thread interface vtable 0x00AC8FC0, the cIGZLua5 interface
//     (vtable 0x00AC8F80, 15 slots) at +0x1C, the table of threads at +0x20
//     and the error reporter at +0x3C.
//
// Here: the Lua 4 tag methods of the thread interface (its last nine
// slots), the protected call and the three chunk loaders with their error
// reports, creation and destruction of both classes, and all of cIGZLua5
// (open and close, threads, garbage collector figures, the error reporter).
// Replacement, off by default:
//
//   scripts.context.part3: the 39 functions below.
//
// Lua itself, the tag library behind the tag methods (0x008BE930 to
// 0x008BF160), the table of threads (0x008BC700 to 0x008BD630) and the
// string helpers stay the game's. What follows this code up to 0x008BA400
// is Lua's own ldebug.c and is not touched (docs/lua.md).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::script_context {
namespace {

// From a thread's cIGZLua5Thread interface (object + 8).
namespace thread {
constexpr std::size_t kId = 0x4;
constexpr std::size_t kState = 0x8;
constexpr std::size_t kLua = 0xC; // the owning cIGZLua5, counted

// Slots of the interface.
constexpr std::size_t kGetLua = 0xC;
constexpr std::size_t kGetTop = 0x38;
constexpr std::size_t kPop = 0x4C;
constexpr std::size_t kType = 0x5C;
constexpr std::size_t kToString = 0xAC;
constexpr std::size_t kToUserData = 0xB8;
constexpr std::size_t kPushString = 0xCC;
constexpr std::size_t kPushCClosure = 0xD0;
constexpr std::size_t kRawSet = 0x104;
constexpr std::size_t kCallProtected = 0x118;
constexpr std::size_t kNewTag = 0x16C;
constexpr std::size_t kCopyTagMethods = 0x174;
} // namespace thread

// From the object's start.
namespace object {
constexpr std::size_t kThreadPart = 0x8;
constexpr std::size_t kId = 0xC;
constexpr std::size_t kState = 0x10;
constexpr std::size_t kLua = 0x14;
constexpr std::size_t kUser = 0x18;
constexpr std::size_t kLuaPart = 0x1C;
constexpr std::size_t kThreads = 0x20;
constexpr std::size_t kReporter = 0x3C;
} // namespace object

// Slots of cIGZLua5.
namespace lua5 {
constexpr std::size_t kGetErrorReporter = 0x38;
} // namespace lua5

constexpr uint32_t kThreadIid = 0xEB2810F1;
constexpr uint32_t kLuaIid = 0x6B2811A6;

constexpr int32_t kGlobalsIndex = -10001;
constexpr int32_t kTypeString = 4;

// cIGZLua5::Open flags.
constexpr uint8_t kOpenIo = 0x2;
constexpr uint8_t kOpenString = 0x4;
constexpr uint8_t kOpenMath = 0x8;
constexpr uint8_t kOpenDebug = 0x10;
constexpr uint8_t kOpenTable = 0x20;
constexpr uint8_t kOpenLua4 = 0x40;

using ErrorReporter = void(__cdecl*)(void* failed, int32_t error, const char* message);

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

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

uint32_t AddRef(void* counted)
{
    return Virtual<uint32_t>(counted, 0x4);
}

uint32_t Release(void* counted)
{
    return Virtual<uint32_t>(counted, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
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

void* State(char* self)
{
    return At<void*>(self, thread::kState);
}

int32_t CallProtectedSlot(char* self, int32_t arguments, int32_t results, int32_t errorFunction, int32_t pop)
{
    return Virtual<int32_t>(self, thread::kCallProtected, arguments, results, errorFunction, pop);
}

// Hands the error on top of the thread's stack to the owner's error reporter.
void Report(char* self, int32_t status)
{
    const char* const message = Virtual<const char*>(self, thread::kToString, int32_t{-1});
    void* const lua = Virtual<void*>(self, thread::kGetLua);
    if (!lua)
        return;
    const ErrorReporter reporter = Virtual<ErrorReporter>(lua, lua5::kGetErrorReporter);
    if (reporter)
        reporter(self, status, message);
}

// The game's 0x008B8400: cIGZLua5Thread::Tag. `self` is the thread interface.
int32_t __fastcall Tag(char* self, void*, int32_t index)
{
    const Precision precision = RaisePrecision();
    Cdecl<void>(addr::Lua_PushValue, State(self), index);
    Cdecl<void>(addr::Lua_PushValue, State(self), int32_t{-1});
    Virtual<void>(self, thread::kPushCClosure, addr::cGZLua5Compat_Tag, int32_t{0});
    Cdecl<void>(addr::Lua_Replace, State(self), int32_t{-3});
    int32_t tag = 0;
    bool found = false;
    if (CallProtectedSlot(self, 1, 1, 0, 1) == 0) {
        tag = Ftol(Cdecl<double>(addr::Lua_ToNumber, State(self), int32_t{-1}));
        Cdecl<void>(addr::Lua_SetTop, State(self), int32_t{-2});
        found = tag != 0;
    }
    if (!found)
        tag = -1;
    RestorePrecision(precision);
    return tag;
}

// The game's 0x008B84B0: cIGZLua5Thread::GetTagMethod.
void __fastcall GetTagMethod(char* self, void*, int32_t tag, const char* event)
{
    const Precision precision = RaisePrecision();
    Virtual<void>(self, thread::kPushCClosure, addr::cGZLua5Compat_GetTagMethod, int32_t{0});
    Cdecl<void>(addr::Lua_PushNumber, State(self), static_cast<double>(tag));
    Cdecl<void>(addr::Lua_PushString, State(self), event);
    if (CallProtectedSlot(self, 2, 1, 0, 1) != 0)
        Cdecl<void>(addr::Lua_PushNil, State(self));
    RestorePrecision(precision);
}

// The game's 0x008B8540: cIGZLua5Thread::SetTagMethod.
void __fastcall SetTagMethod(char* self, void*, int32_t tag, const char* event)
{
    const Precision precision = RaisePrecision();
    Virtual<void>(self, thread::kPushCClosure, addr::cGZLua5Compat_SetTagMethod, int32_t{0});
    Cdecl<void>(addr::Lua_PushNumber, State(self), static_cast<double>(tag));
    Cdecl<void>(addr::Lua_PushString, State(self), event);
    Cdecl<void>(addr::Lua_PushValue, State(self), int32_t{-4});
    CallProtectedSlot(self, 3, 0, 0, 1);
    Cdecl<void>(addr::Lua_SetTop, State(self), int32_t{-2});
    RestorePrecision(precision);
}

// The game's 0x008B85E0: cIGZLua5Thread::NewTag. The result is read at
// stack index 1, not at the top.
int32_t __fastcall NewTag(char* self, void*)
{
    const Precision precision = RaisePrecision();
    int32_t tag = -1;
    Virtual<void>(self, thread::kPushCClosure, addr::cGZLua5Compat_NewTag, int32_t{0});
    if (CallProtectedSlot(self, 0, 1, 0, 1) == 0) {
        tag = Ftol(Cdecl<double>(addr::Lua_ToNumber, State(self), int32_t{1}));
        Cdecl<void>(addr::Lua_SetTop, State(self), int32_t{-2});
        if (tag <= 0)
            tag = -1;
    }
    RestorePrecision(precision);
    return tag;
}

// The game's 0x008B8670: cIGZLua5Thread::SetTag.
void __fastcall SetTag(char* self, void*, int32_t tag)
{
    const Precision precision = RaisePrecision();
    Cdecl<void>(addr::cGZLua5Compat_SetTag, State(self), tag);
    RestorePrecision(precision);
}

// The game's 0x008B86C0: cIGZLua5Thread::CopyTagMethods.
int32_t __fastcall CopyTagMethods(char* self, void*, int32_t to, int32_t from)
{
    const Precision precision = RaisePrecision();
    Virtual<void>(self, thread::kPushCClosure, addr::cGZLua5Compat_CopyTagMethods, int32_t{0});
    Cdecl<void>(addr::Lua_PushNumber, State(self), static_cast<double>(to));
    Cdecl<void>(addr::Lua_PushNumber, State(self), static_cast<double>(from));
    CallProtectedSlot(self, 2, 0, 0, 1);
    RestorePrecision(precision);
    return to;
}

// The game's 0x008B8750: cIGZLua5Thread::CloneTag: a new tag with the
// methods of `tag`.
int32_t __fastcall CloneTag(char* self, void*, int32_t tag)
{
    const Precision precision = RaisePrecision();
    const int32_t fresh = Virtual<int32_t>(self, thread::kNewTag);
    const int32_t result = Virtual<int32_t>(self, thread::kCopyTagMethods, fresh, tag);
    RestorePrecision(precision);
    return result;
}

// The game's 0x008B87A0: cIGZLua5Thread::PushUserTag.
void __fastcall PushUserTag(char* self, void*, void* data, int32_t tag)
{
    const Precision precision = RaisePrecision();
    Cdecl<void>(addr::cGZLua5Compat_PushUserTag, State(self), data, tag);
    RestorePrecision(precision);
}

// The game's 0x008B87F0: cIGZLua5Thread::ToUserTag: the pointer kept in the
// user data at `index` (no check that there is one).
void* __fastcall ToUserTag(char* self, void*, int32_t index)
{
    const Precision precision = RaisePrecision();
    void* const data = *Virtual<void**>(self, thread::kToUserData, index);
    RestorePrecision(precision);
    return data;
}

// The game's 0x008B8840: cGZLua5Thread::QueryInterface. `self` is the object.
uint8_t __fastcall ThreadQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid != kThreadIid)
        return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
    *out = self ? self + object::kThreadPart : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x008B8CF0: cGZLua5::QueryInterface. `self` is the object.
uint8_t __fastcall LuaQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == kLuaIid) {
        *out = self ? self + object::kLuaPart : nullptr;
        AddRef(self);
        return 1;
    }
    *out = nullptr;
    if (iid != kThreadIid)
        return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
    *out = self ? self + object::kThreadPart : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x008B8E10: QueryInterface through the thread interface.
uint8_t __fastcall ThreadQueryInterfaceAt8(char* self, void*, uint32_t iid, void** out)
{
    return ThreadQueryInterface(self - object::kThreadPart, nullptr, iid, out);
}

// The game's 0x008B92C0: cGZLua5's QueryInterface through the thread interface.
uint8_t __fastcall LuaQueryInterfaceAt8(char* self, void*, uint32_t iid, void** out)
{
    return LuaQueryInterface(self - object::kThreadPart, nullptr, iid, out);
}

// The game's 0x008B92D0: QueryInterface through cIGZLua5.
uint8_t __fastcall LuaQueryInterfaceAt1C(char* self, void*, uint32_t iid, void** out)
{
    return LuaQueryInterface(self - object::kLuaPart, nullptr, iid, out);
}

// The game's 0x008B92E0: AddRef through cIGZLua5.
uint32_t __fastcall LuaAddRefAt1C(char* self, void*)
{
    return This<uint32_t>(addr::cRZBaseUnknown_AddRefJump, self - object::kLuaPart);
}

// The game's 0x008B92F0: Release through cIGZLua5.
uint32_t __fastcall LuaReleaseAt1C(char* self, void*)
{
    return This<uint32_t>(addr::cRZBaseUnknown_ReleaseJump, self - object::kLuaPart);
}

// The game's 0x008B8890: cIGZLua5::Open. `self` is the cIGZLua5 interface
// (object + 0x1C), as in every cIGZLua5 method below.
uint8_t __fastcall Open(char* self, void*, uint32_t flagsWord)
{
    char* const whole = self - object::kLuaPart;
    const uint8_t flags = static_cast<uint8_t>(flagsWord);
    const Precision precision = RaisePrecision();
    void* fresh = nullptr;
    if (At<void*>(whole, object::kState) || !(fresh = Cdecl<void*>(addr::Lua_Open))
        || !This<uint8_t>(addr::SCLuaThreads_Open, whole + object::kThreads, static_cast<void*>(self), fresh)) {
        RestorePrecision(precision);
        return 0;
    }
    Cdecl<void>(addr::Lua_OpenBase, At<void*>(whole, object::kState));
    if (flags & kOpenIo)
        Cdecl<void>(addr::Lua_OpenIO, At<void*>(whole, object::kState));
    if (flags & kOpenString)
        Cdecl<void>(addr::Lua_OpenString, At<void*>(whole, object::kState));
    if (flags & kOpenMath)
        Cdecl<void>(addr::Lua_OpenMath, At<void*>(whole, object::kState));
    if (flags & kOpenDebug)
        Cdecl<void>(addr::Lua_OpenDebug, At<void*>(whole, object::kState));
    if (flags & kOpenTable)
        Cdecl<void>(addr::Lua_OpenTable, At<void*>(whole, object::kState));
    if (flags & kOpenLua4)
        Cdecl<void>(addr::cGZLua5Compat_Open, static_cast<void*>(self));
    Cdecl<void>(addr::cGZLua5_OpenExtras, static_cast<void*>(self));
    Cdecl<void>(addr::Lua_SetTop, At<void*>(whole, object::kState), int32_t{0});
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008B89B0: cIGZLua5::Close.
uint8_t __fastcall Close(char* self, void*)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    void* const state = At<void*>(whole, object::kState);
    if (state) {
        This<uint8_t>(addr::SCLuaThreads_Close, whole + object::kThreads);
        Cdecl<void>(addr::Lua_Close, state);
    }
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008B8A00: cIGZLua5::CreateThread.
int32_t __fastcall NewThread(char* self, void*, void** out, int32_t flags, uint32_t user)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    *out = nullptr;
    char* const threads = whole + object::kThreads;
    const int32_t id =
        This<int32_t>(addr::SCLuaThreads_Create, threads, At<void*>(whole, object::kState), flags, user);
    if (id != -1) {
        void** const state = This<void**>(addr::SCLuaThreads_Find, threads, id);
        void* const created = state[-1];
        *out = created;
        AddRef(created);
    }
    RestorePrecision(precision);
    return id;
}

// The game's 0x008B8A80: cIGZLua5::GetThread.
void* __fastcall FindThread(char* self, void*, int32_t id)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    void** const state = This<void**>(addr::SCLuaThreads_Find, whole + object::kThreads, id);
    void* const found = state ? state[-1] : nullptr;
    RestorePrecision(precision);
    return found;
}

// The game's 0x008B8AE0: cIGZLua5::DestroyThread.
uint8_t __fastcall EndThread(char* self, void*, int32_t id)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    const uint8_t done =
        This<uint8_t>(addr::SCLuaThreads_EndThread, whole + object::kThreads, At<void*>(whole, object::kState), id);
    RestorePrecision(precision);
    return done;
}

// The game's 0x008B8B30: cIGZLua5::ResumeThread. Without `results` the
// thread's results are dropped from the main stack.
uint8_t __fastcall Resume(char* self, void*, int32_t id, int32_t arguments, int32_t* results)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    const int32_t base = Cdecl<int32_t>(addr::Lua_GetTop, At<void*>(whole, object::kState)) - arguments;
    char* const threads = whole + object::kThreads;
    const int32_t status =
        This<int32_t>(addr::SCLuaThreads_Resume, threads, At<void*>(whole, object::kState), id, arguments);
    if (status != 0 && At<ErrorReporter>(whole, object::kReporter)) {
        void** const state = This<void**>(addr::SCLuaThreads_Find, threads, id);
        void* const failed = state ? state[-1] : nullptr;
        const char* message = Cdecl<const char*>(addr::Lua_ToString, At<void*>(whole, object::kState), int32_t{-1});
        if (!message)
            message = "<unknown description>";
        At<ErrorReporter>(whole, object::kReporter)(failed, status, message);
    }
    const int32_t count = Cdecl<int32_t>(addr::Lua_GetTop, At<void*>(whole, object::kState)) - base;
    if (!results)
        Cdecl<void>(addr::Lua_SetTop, At<void*>(whole, object::kState), -1 - count);
    else
        *results = count;
    const uint8_t done = status == 0;
    RestorePrecision(precision);
    return done;
}

// The game's 0x008B8C20: cIGZLua5::GetGCCount.
int32_t __fastcall GetGCCount(char* self, void*)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    const int32_t count = Cdecl<int32_t>(addr::Lua_GetGCCount, At<void*>(whole, object::kState));
    RestorePrecision(precision);
    return count;
}

// The game's 0x008B8C60: cIGZLua5::GetGCThreshold.
int32_t __fastcall GetGCThreshold(char* self, void*)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    const int32_t threshold = Cdecl<int32_t>(addr::Lua_GetGCThreshold, At<void*>(whole, object::kState));
    RestorePrecision(precision);
    return threshold;
}

// The game's 0x008B8CA0: cIGZLua5::SetGCThreshold.
void __fastcall SetGCThreshold(char* self, void*, int32_t threshold)
{
    char* const whole = self - object::kLuaPart;
    const Precision precision = RaisePrecision();
    Cdecl<void>(addr::Lua_SetGCThreshold, At<void*>(whole, object::kState), threshold);
    RestorePrecision(precision);
}

// The game's 0x008B96D0: cIGZLua5::SetErrorReporter.
void __fastcall SetErrorReporter(char* self, void*, std::uintptr_t reporter, uint32_t useDefaultWord)
{
    const uint8_t useDefault = static_cast<uint8_t>(useDefaultWord);
    char* const whole = self - object::kLuaPart;
    At<std::uintptr_t>(whole, object::kReporter) = reporter;
    if (!reporter && useDefault)
        At<std::uintptr_t>(whole, object::kReporter) = addr::cSCLua_F8B9360;
}

// The game's 0x008B8D70: the script function `_ALERT` (a Lua C function):
// passes the message on top of the stack to the error reporter as a run-time
// error.
int32_t __cdecl Alert(void** state)
{
    char* const self = static_cast<char*>(state[-1]);
    if (!self)
        return 0;
    void* const lua = Virtual<void*>(self, thread::kGetLua);
    if (!lua)
        return 0;
    const ErrorReporter reporter = Virtual<ErrorReporter>(lua, lua5::kGetErrorReporter);
    if (reporter && Virtual<int32_t>(self, thread::kType, int32_t{-1}) == kTypeString)
        reporter(self, 1, Virtual<const char*>(self, thread::kToString, int32_t{-1}));
    return 0;
}

// The game's 0x008B8DD0: cGZLua5Thread's constructor.
char* __fastcall ThreadConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cIGZLua5Thread_PureVtable;
    At<void*>(self, object::kState) = nullptr;
    At<std::uintptr_t>(self, 0) = addr::cGZLua5Thread_Vtable;
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cGZLua5Thread_ThreadVtable;
    At<int32_t>(self, object::kId) = -1;
    At<void*>(self, object::kLua) = nullptr;
    At<void*>(self, object::kUser) = nullptr;
    return self;
}

// The game's 0x008B8E20: cIGZLua5Thread::OnCreate: takes the state and its
// owner and sets the script function `_ALERT`.
uint8_t __fastcall OnCreate(char* self, void*, void* lua, void* state, int32_t id)
{
    const Precision precision = RaisePrecision();
    At<int32_t>(self, thread::kId) = id;
    At<void*>(self, thread::kState) = state;
    void* const previous = At<void*>(self, thread::kLua);
    if (lua != previous) {
        if (lua)
            AddRef(lua);
        At<void*>(self, thread::kLua) = lua;
        if (previous)
            Release(previous);
    }
    Virtual<void>(self, thread::kPushString, "_ALERT");
    Virtual<void>(self, thread::kPushCClosure, addr::cSCLua_F8B8D70, int32_t{0});
    Virtual<void>(self, thread::kRawSet, kGlobalsIndex);
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008B8ED0: cIGZLua5Thread::OnDestroy.
uint8_t __fastcall OnDestroy(char* self, void*)
{
    const Precision precision = RaisePrecision();
    At<void*>(self, thread::kState) = nullptr;
    void* const lua = At<void*>(self, thread::kLua);
    if (lua) {
        At<void*>(self, thread::kLua) = nullptr;
        Release(lua);
    }
    At<int32_t>(self, thread::kId) = -1;
    RestorePrecision(precision);
    return 1;
}

// The game's 0x008B8F30: cIGZLua5Thread::CallProtected. Without an error
// function, and with an error reporter set, the script's `_TRACEBACK` is the
// error function. A failure goes to the error reporter.
int32_t __fastcall CallProtected(char* self, void*, int32_t arguments, int32_t results, int32_t errorFunction,
                                 uint32_t popWord)
{
    const uint8_t pop = static_cast<uint8_t>(popWord);
    const Precision precision = RaisePrecision();
    Virtual<int32_t>(self, thread::kGetTop);
    int32_t status;
    void* lua;
    if (errorFunction == 0 && (lua = At<void*>(self, thread::kLua)) != nullptr
        && Virtual<ErrorReporter>(lua, lua5::kGetErrorReporter)) {
        const int32_t base = Virtual<int32_t>(self, thread::kGetTop) - arguments;
        Cdecl<void>(addr::Lua_PushLString, State(self), "_TRACEBACK", uint32_t{10});
        Cdecl<void>(addr::Lua_RawGet, State(self), kGlobalsIndex);
        Cdecl<void>(addr::Lua_Insert, State(self), base);
        status = Cdecl<int32_t>(addr::Lua_PCall, State(self), arguments, results, base);
        Cdecl<void>(addr::Lua_Remove, State(self), base);
    } else {
        status = Cdecl<int32_t>(addr::Lua_PCall, State(self), arguments, results, errorFunction);
    }
    if (status != 0) {
        Report(self, status);
        if (pop)
            Virtual<void>(self, thread::kPop, int32_t{1});
    }
    RestorePrecision(precision);
    return status;
}

// After a failed load: reports the message on top of the stack when it is a
// string.
void ReportLoad(char* self, int32_t status)
{
    if (status != 0 && Virtual<int32_t>(self, thread::kType, int32_t{-1}) == kTypeString)
        Report(self, status);
}

// The game's 0x008B9050: cIGZLua5Thread::LoadFile.
int32_t __fastcall LoadFile(char* self, void*, const char* name)
{
    const Precision precision = RaisePrecision();
    const int32_t status = Cdecl<int32_t>(addr::LuaL_LoadFile, State(self), name);
    ReportLoad(self, status);
    RestorePrecision(precision);
    return status;
}

// The game's 0x008B90F0: cIGZLua5Thread::LoadString: the text is its own
// chunk name.
int32_t __fastcall LoadText(char* self, void*, const char* text)
{
    const Precision precision = RaisePrecision();
    uint32_t length = 0;
    while (text[length])
        ++length;
    const int32_t status = Cdecl<int32_t>(addr::LuaL_LoadBuffer, State(self), text, length, text);
    ReportLoad(self, status);
    RestorePrecision(precision);
    return status;
}

// The game's 0x008B91A0: cIGZLua5Thread::LoadBuffer.
int32_t __fastcall LoadBuffer(char* self, void*, const char* buffer, uint32_t size, const char* name)
{
    const Precision precision = RaisePrecision();
    const int32_t status = Cdecl<int32_t>(addr::LuaL_LoadBuffer, State(self), buffer, size, name);
    ReportLoad(self, status);
    RestorePrecision(precision);
    return status;
}

// The game's 0x008B9240: cGZLua5's destructor. `self` is the object.
void __fastcall LuaDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cGZLua5_Vtable;
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cGZLua5_ThreadVtable;
    At<std::uintptr_t>(self, object::kLuaPart) = addr::cGZLua5_LuaVtable;
    const Precision precision = RaisePrecision();
    if (At<void*>(self, object::kState))
        Close(self + object::kLuaPart, nullptr);
    RestorePrecision(precision);
    This<void>(addr::SCLuaThreads_Destruct, self + object::kThreads);
    At<std::uintptr_t>(self, 0) = addr::cGZLua5Thread_Vtable;
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cGZLua5Thread_ThreadVtable;
    void* const lua = At<void*>(self, object::kLua);
    if (lua)
        Release(lua);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// The game's 0x008B9300: cGZLua5Thread's deleting destructor.
char* __fastcall ThreadDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::cGZLua5Thread_Vtable;
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cGZLua5Thread_ThreadVtable;
    void* const lua = At<void*>(self, object::kLua);
    if (lua)
        Release(lua);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x008B9340: cGZLua5's deleting destructor.
char* __fastcall LuaDelete(char* self, void*, uint32_t flags)
{
    LuaDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x008B9360: the default error reporter. It composes the whole
// report (heading, thread ID, kind of error, the message between two rules)
// and then drops it: nothing shows or logs the text in this build.
void __cdecl DefaultErrorReporter(void* failed, int32_t error, const char* message)
{
    static const char kHeading[] = "GZLua5 script error !\n\n";
    static const char kUnknown[] = "Unknown\n";
    static const char kThread[] = "  Thread ID :\t";
    // A new line, 91 equal signs, a new line; the second has one more new line.
    static const char kRuleAbove[] = "\n=============================================="
                                     "=============================================\n";
    static const char kRuleBelow[] = "\n=============================================="
                                     "=============================================\n\n";
    char tag = 0;

    GameString report;
    report.body = {};
    This<void>(addr::SC4String_FromRange, &report.body, kHeading, kHeading + sizeof(kHeading) - 1, &tag);
    report.vtable = addr::SC4String_Vtable;
    report.zero = 0;
    GameString kind;
    kind.body = {};
    This<void>(addr::SC4String_FromRange, &kind.body, kUnknown, kUnknown + sizeof(kUnknown) - 1, &tag);
    kind.vtable = addr::SC4String_Vtable;
    kind.zero = 0;

    const char* name = nullptr;
    switch (error) {
    case 1:
        name = "Run-time\n";
        break;
    case 2:
        name = "File-related\n";
        break;
    case 3:
        name = "Syntax\n";
        break;
    case 4:
        name = "Allocation\n";
        break;
    case 5:
        name = kUnknown;
        break;
    default:
        break;
    }
    if (name) {
        GameString text;
        This<void*>(addr::SC4String_FromText, &text, name);
        This<void*>(addr::cRZBaseString_Assign, &kind, &text);
        if (text.body.begin)
            Free(text.body.begin);
    }

    This<void*>(addr::cSC4Advisor_F40EBC0, &report.body, kThread, kThread + sizeof(kThread) - 1, &tag);
    if (failed) {
        GameString number;
        number.body = {};
        number.body.begin = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        number.body.end = number.body.begin;
        number.body.capacity = number.body.begin + 8;
        *number.body.begin = 0;
        number.vtable = addr::SC4String_Vtable;
        number.zero = 0;
        const int32_t id = Virtual<int32_t>(failed, 0x28); // GetID
        using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
        void* const formatted = reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(&number, "%i\n", id);
        const uint32_t length = Virtual<uint32_t>(formatted, 0x1C);
        const char* const characters = Virtual<const char*>(formatted, 0x18);
        StringBody piece = {};
        This<void>(addr::SC4String_FromRange, &piece, characters, characters + length, &tag);
        char* const pieceBegin = piece.begin;
        This<void*>(addr::SC4String_AppendRange, &report.body, pieceBegin, piece.end, &tag);
        if (pieceBegin)
            Free(pieceBegin);
        number.vtable = addr::SC4String_Vtable;
        if (number.body.begin)
            Free(number.body.begin);
    } else {
        This<void*>(addr::cSC4Advisor_F40EBC0, &report.body, kUnknown, kUnknown + sizeof(kUnknown) - 1, &tag);
    }

    StringBody typeLine;
    const StringBody* joined =
        Cdecl<StringBody*>(addr::SC4String_ConcatTextBody, &typeLine, " Error Type :\t", &kind.body);
    This<void*>(addr::SC4String_AppendRange, &report.body, joined->begin, joined->end, &tag);
    if (typeLine.begin)
        Free(typeLine.begin);

    GameString shown;
    shown.body = {};
    const char* messageEnd = message;
    while (*messageEnd)
        ++messageEnd;
    This<void>(addr::SC4String_FromRange, &shown.body, message, messageEnd, &tag);
    shown.vtable = addr::SC4String_Vtable;
    shown.zero = 0;
    StringBody opened;
    StringBody closed;
    const StringBody* const withRule =
        Cdecl<StringBody*>(addr::SC4String_ConcatTextBody, &opened, kRuleAbove, &shown.body);
    joined = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &closed, withRule, kRuleBelow);
    This<void*>(addr::SC4String_AppendRange, &report.body, joined->begin, joined->end, &tag);
    if (closed.begin)
        Free(closed.begin);
    if (opened.begin)
        Free(opened.begin);
    if (shown.body.begin)
        Free(shown.body.begin);
    if (kind.body.begin)
        Free(kind.body.begin);
    if (report.body.begin)
        Free(report.body.begin);
}

// The game's 0x008B9670: cGZLua5's constructor. `self` is the object.
char* __fastcall LuaConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cIGZLua5Thread_PureVtable;
    At<int32_t>(self, object::kId) = -1;
    At<void*>(self, object::kState) = nullptr;
    At<void*>(self, object::kLua) = nullptr;
    At<void*>(self, object::kUser) = nullptr;
    At<std::uintptr_t>(self, object::kLuaPart) = addr::cIGZLua5_PureVtable;
    At<std::uintptr_t>(self, 0) = addr::cGZLua5_Vtable;
    At<std::uintptr_t>(self, object::kThreadPart) = addr::cGZLua5_ThreadVtable;
    At<std::uintptr_t>(self, object::kLuaPart) = addr::cGZLua5_LuaVtable;
    This<void*>(addr::SCLuaThreads_Construct, self + object::kThreads);
    At<void*>(self, object::kState) = nullptr;
    At<std::uintptr_t>(self, object::kReporter) = addr::cSCLua_F8B9360;
    return self;
}

bool InstallPart3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[39] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSCLua_F8B8400, reinterpret_cast<void*>(&Tag)},
        {addr::cSCLua_F8B84B0, reinterpret_cast<void*>(&GetTagMethod)},
        {addr::cSCLua_F8B8540, reinterpret_cast<void*>(&SetTagMethod)},
        {addr::cSCLua_F8B85E0, reinterpret_cast<void*>(&NewTag)},
        {addr::cSCLua_F8B8670, reinterpret_cast<void*>(&SetTag)},
        {addr::cSCLua_F8B86C0, reinterpret_cast<void*>(&CopyTagMethods)},
        {addr::cSCLua_F8B8750, reinterpret_cast<void*>(&CloneTag)},
        {addr::cSCLua_F8B87A0, reinterpret_cast<void*>(&PushUserTag)},
        {addr::cSCLua_F8B87F0, reinterpret_cast<void*>(&ToUserTag)},
        {addr::cSCLua_F8B8840, reinterpret_cast<void*>(&ThreadQueryInterface)},
        {addr::cSCLua_F8B8890, reinterpret_cast<void*>(&Open)},
        {addr::cSCLua_F8B89B0, reinterpret_cast<void*>(&Close)},
        {addr::cSCLua_F8B8A00, reinterpret_cast<void*>(&NewThread)},
        {addr::cSCLua_F8B8A80, reinterpret_cast<void*>(&FindThread)},
        {addr::cSCLua_F8B8AE0, reinterpret_cast<void*>(&EndThread)},
        {addr::cSCLua_F8B8B30, reinterpret_cast<void*>(&Resume)},
        {addr::cSCLua_F8B8C20, reinterpret_cast<void*>(&GetGCCount)},
        {addr::cSCLua_F8B8C60, reinterpret_cast<void*>(&GetGCThreshold)},
        {addr::cSCLua_F8B8CA0, reinterpret_cast<void*>(&SetGCThreshold)},
        {addr::cSCLua_F8B8CF0, reinterpret_cast<void*>(&LuaQueryInterface)},
        {addr::cSCLua_F8B8D70, reinterpret_cast<void*>(&Alert)},
        {addr::cSCLua_F8B8DD0, reinterpret_cast<void*>(&ThreadConstruct)},
        {addr::cSCLua_F8B8E10, reinterpret_cast<void*>(&ThreadQueryInterfaceAt8)},
        {addr::cSCLua_F8B8E20, reinterpret_cast<void*>(&OnCreate)},
        {addr::cSCLua_F8B8ED0, reinterpret_cast<void*>(&OnDestroy)},
        {addr::cSCLua_F8B8F30, reinterpret_cast<void*>(&CallProtected)},
        {addr::cSCLua_F8B9050, reinterpret_cast<void*>(&LoadFile)},
        {addr::cSCLua_F8B90F0, reinterpret_cast<void*>(&LoadText)},
        {addr::cSCLua_F8B91A0, reinterpret_cast<void*>(&LoadBuffer)},
        {addr::cSCLua_F8B9240, reinterpret_cast<void*>(&LuaDestruct)},
        {addr::cSCLua_F8B92C0, reinterpret_cast<void*>(&LuaQueryInterfaceAt8)},
        {addr::cSCLua_F8B92D0, reinterpret_cast<void*>(&LuaQueryInterfaceAt1C)},
        {addr::cSCLua_F8B92E0, reinterpret_cast<void*>(&LuaAddRefAt1C)},
        {addr::cSCLua_F8B92F0, reinterpret_cast<void*>(&LuaReleaseAt1C)},
        {addr::cSCLua_F8B9300, reinterpret_cast<void*>(&ThreadDelete)},
        {addr::cSCLua_F8B9340, reinterpret_cast<void*>(&LuaDelete)},
        {addr::cSCLua_F8B9360, reinterpret_cast<void*>(&DefaultErrorReporter)},
        {addr::cSCLua_F8B9670, reinterpret_cast<void*>(&LuaConstruct)},
        {addr::cSCLua_F8B96D0, reinterpret_cast<void*>(&SetErrorReporter)},
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

const bool g_part3Registered = registry::Register({
    "scripts.context.part3",
    "scripts/context",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSCLua_F8B8F30,
    "Our Lua wrappers under the script context (cGZLua5Thread, cGZLua5): tag methods, protected call, chunk "
    "loaders, threads, creation and the error reporter (docs/lua.md)",
    false,
    InstallPart3,
});

} // namespace
} // namespace opensc4::script_context
