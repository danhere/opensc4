// The script context the game uses, part 1 (0x0087FFF0 to 0x00881400): the
// class derived from the base Lua 5.0 context (class 0x4B2811AA, 0x58 bytes),
// with the director that registers the script classes in front of it.
// Replacement, off by default:
//
//   scripts.context.part1: the director (its ID, the three factories, the
//     constructor, the deleting destructor, the instance getter), the
//     context's constructor and destructors, QueryInterface, Open and Close,
//     GetN, Ref, CallProtected, PushNumber, the three data registry
//     conversions, the registry walk callback, the value exchange closure,
//     and every forwarding thunk of its vtables.
//
// The object: the counted base's vtable at +0; the thread part at +8 (main
// vtable 0x00AC42C0, the Lua state at +0x10); the engine part at +0x1C
// (Open, Close, the collector's figures); the script interface (cISCLua) at
// +0x40, which is what the game's global at 0x00B43CE8 points at; a
// notification target at +0x44; the "open" byte at +0x48; two parameter sets
// at +0x4C and +0x50; a list at +0x54.
//
// Most of the class is thunks. A first-level thunk is a bare `jmp` into the
// base context (parts 2 and 3) and takes the part of the object its target
// takes; a second-level thunk is a slot of the script interface, moves
// `this` back to that part and jumps to a first-level one. Both are written
// with the macros below, one line each with the whole signature. The base
// context's functions are called through their addresses.
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

namespace part { // from the object's start
constexpr std::size_t kThread = 0x8;
constexpr std::size_t kEngine = 0x1C;
constexpr std::size_t kScript = 0x40;
constexpr std::size_t kTarget = 0x44;
} // namespace part

namespace field { // from the object's start
constexpr std::size_t kOpened = 0x48;
constexpr std::size_t kInput = 0x4C;  // counted, 0x28 bytes, made by Open
constexpr std::size_t kOutput = 0x50; // the same
constexpr std::size_t kList = 0x54;   // a list's sentinel node
} // namespace field

constexpr uint32_t kScriptIid = 0xEA10C4A2;
constexpr uint32_t kDirectorId = 0xCA107F8B;
constexpr int32_t kRegistryIndex = -10000; // Lua's LUA_REGISTRYINDEX

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
    uint16_t type; // 1 a byte, 2 to 14 numbers, 15 a string, 16 a counted object
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

// `jmp target`, after moving `this` by `adjust` bytes: every argument goes
// on to the game's function at `target`, whose result comes back unchanged.
#define OPENSC4_JUMP0(name, R, adjust, target) \
    R __fastcall name(char* self, void*) \
    { \
        return This<R>(target, self + (adjust)); \
    }
#define OPENSC4_JUMP1(name, R, A, adjust, target) \
    R __fastcall name(char* self, void*, A a) \
    { \
        return This<R>(target, self + (adjust), a); \
    }
#define OPENSC4_JUMP2(name, R, A, B, adjust, target) \
    R __fastcall name(char* self, void*, A a, B b) \
    { \
        return This<R>(target, self + (adjust), a, b); \
    }
#define OPENSC4_JUMP3(name, R, A, B, C, adjust, target) \
    R __fastcall name(char* self, void*, A a, B b, C c) \
    { \
        return This<R>(target, self + (adjust), a, b, c); \
    }

// The same where the target is a function of this file.
#define OPENSC4_VIA0(name, R, adjust, function) \
    R __fastcall name(char* self, void*) \
    { \
        return function(self + (adjust), nullptr); \
    }
#define OPENSC4_VIA1(name, R, A, adjust, function) \
    R __fastcall name(char* self, void*, A a) \
    { \
        return function(self + (adjust), nullptr, a); \
    }
#define OPENSC4_VIA2(name, R, A, B, adjust, function) \
    R __fastcall name(char* self, void*, A a, B b) \
    { \
        return function(self + (adjust), nullptr, a, b); \
    }
#define OPENSC4_VIA3(name, R, A, B, C, adjust, function) \
    R __fastcall name(char* self, void*, A a, B b, C c) \
    { \
        return function(self + (adjust), nullptr, a, b, c); \
    }

// ---------------------------------------------------------------------------
// The director of the script classes (0x0087FFF0 to 0x008801C0).

uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object);
char* __fastcall Construct(char* self, void*);

// The game's 0x0087FFF0. `this` is the director.
uint32_t __fastcall DirectorId(char*, void*)
{
    return kDirectorId;
}

// What the three factories do with their new object: a reference, the
// caller's interface, the reference back. A failed allocation is not checked
// (the original calls through the null pointer too).
uint8_t Deliver(char* created, uint32_t iid, void** object)
{
    Virtual<uint32_t>(created, 0x4);
    const uint8_t found = Virtual<uint8_t>(created, 0x0, iid, object);
    Virtual<uint32_t>(created, 0x8);
    return found;
}

// The game's 0x00880000: the factory of class 0x2BD13FB4.
uint8_t __cdecl CreateClass2BD13FB4(uint32_t iid, void** object)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x58});
    char* const created = memory ? This<char*>(addr::SC4ScriptClass2BD13FB4_Construct, memory) : nullptr;
    return Deliver(created, iid, object);
}

// The game's 0x00880050: the factory of the base context's thread class
// (0x2B2810F7).
uint8_t __cdecl CreateThread(uint32_t iid, void** object)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1C});
    char* const created = memory ? This<char*>(addr::cSCLua_F8B8DD0, memory) : nullptr;
    return Deliver(created, iid, object);
}

// The game's 0x008800A0: the factory of the script context (classes
// 0x4B2811AA and 0xEA10C4A2).
uint8_t __cdecl CreateContext(uint32_t iid, void** object)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x58});
    char* const created = memory ? Construct(static_cast<char*>(memory), nullptr) : nullptr;
    return Deliver(created, iid, object);
}

// The game's 0x008800F0: the director's constructor. `this` is the director.
char* __fastcall DirectorConstruct(char* self, void*)
{
    This<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<std::uintptr_t>(self, 0x0) = addr::cSCLuaDirector_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSCLuaDirector_FrameworkHookVtable;
    const auto add = [self](uint32_t classId, std::uintptr_t factory) {
        This<void>(addr::cRZCOMDllDirector_AddCls_FactoryType2, self, classId, factory);
    };
    add(0x2BD13FB4, addr::cSCLua_F880000);
    add(0x2B2810F7, addr::cSCLua_F880050);
    add(0x4B2811AA, addr::cSCLua_F8800A0);
    add(kScriptIid, addr::cSCLua_F8800A0);
    return self;
}

// The game's 0x00880150: the director's deleting destructor.
char* __fastcall DirectorDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSCLuaDirector_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSCLuaDirector_FrameworkHookVtable;
    This<void>(addr::cRZCOMDllDirector_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00880180: the director, constructed at the first call.
char* __cdecl GetDirector()
{
    char* const director = reinterpret_cast<char*>(addr::cSCLuaDirector_Instance);
    if (!(Global<uint8_t>(addr::cSCLuaDirector_InstanceGuard) & 1)) {
        Global<uint32_t>(addr::cSCLuaDirector_InstanceGuard) |= 1;
        DirectorConstruct(director, nullptr);
        Cdecl<int32_t>(addr::SC4_atexit, addr::cSCLuaDirector_AtExit);
    }
    return director;
}

// ---------------------------------------------------------------------------
// First-level thunks (0x008801C0 to 0x008806C0). In `thread`, `this` is the
// thread part (+8); in `engine` the engine part (+0x1C); in `script` the
// script interface (+0x40), which these move back to the thread part.

namespace thread {
OPENSC4_JUMP0(GetTop, int32_t, 0, addr::cSCLua_F8B6DF0) // 0x008801C0
OPENSC4_JUMP1(SetTop, void, int32_t, 0, addr::cSCLua_F8B6E30) // 0x008801D0
OPENSC4_JUMP1(PushValue, void, int32_t, 0, addr::cSCLua_F8B6E80) // 0x008801E0
OPENSC4_JUMP1(Remove, void, int32_t, 0, addr::cSCLua_F8B6ED0) // 0x008801F0
OPENSC4_JUMP1(Insert, void, int32_t, 0, addr::cSCLua_F8B6F20) // 0x00880200
OPENSC4_JUMP1(Pop, void, int32_t, 0, addr::cSCLua_F8B6F70) // 0x00880210
OPENSC4_JUMP1(Type, int32_t, int32_t, 0, addr::cSCLua_F8B70B0) // 0x00880220
OPENSC4_JUMP1(TypeName, const char*, int32_t, 0, addr::cSCLua_F8B7100) // 0x00880230
OPENSC4_JUMP1(IsNumber, uint8_t, int32_t, 0, addr::cSCLua_F8B7150) // 0x00880240
OPENSC4_JUMP1(IsBoolean, uint8_t, int32_t, 0, addr::cSCLua_F8B73B0) // 0x00880250
OPENSC4_JUMP1(IsString, uint8_t, int32_t, 0, addr::cSCLua_F8B71A0) // 0x00880260
OPENSC4_JUMP1(IsCFunction, uint8_t, int32_t, 0, addr::cSCLua_F8B71F0) // 0x00880270
OPENSC4_JUMP1(Tag, int32_t, int32_t, 0, addr::cSCLua_F8B8400) // 0x00880280
OPENSC4_JUMP2(Equal, uint8_t, int32_t, int32_t, 0, addr::cSCLua_F8B7520) // 0x00880290
OPENSC4_JUMP2(LessThan, uint8_t, int32_t, int32_t, 0, addr::cSCLua_F8B75E0) // 0x008802A0
OPENSC4_JUMP1(ToInteger, int32_t, int32_t, 0, addr::cSCLua_F8B7640) // 0x008802B0
OPENSC4_JUMP1(ToNumber, double, int32_t, 0, addr::cSCLua_F8B7690) // 0x008802C0
OPENSC4_JUMP1(ToString, const char*, int32_t, 0, addr::cSCLua_F8B7740) // 0x008802D0
OPENSC4_JUMP1(Strlen, uint32_t, int32_t, 0, addr::cSCLua_F8B7790) // 0x008802E0
OPENSC4_JUMP1(ToCFunction, void*, int32_t, 0, addr::cSCLua_F8B77E0) // 0x008802F0
OPENSC4_JUMP1(ToUserData, void*, int32_t, 0, addr::cSCLua_F8B7830) // 0x00880300
OPENSC4_JUMP1(ToUserTag, uint32_t, int32_t, 0, addr::cSCLua_F8B87F0) // 0x00880310
OPENSC4_JUMP1(ToBoolean, uint8_t, int32_t, 0, addr::cSCLua_F8B76F0) // 0x00880320
OPENSC4_JUMP0(PushNil, void, 0, addr::cSCLua_F8B78D0) // 0x00880330
OPENSC4_JUMP2(PushLString, void, const char*, uint32_t, 0, addr::cSCLua_F8B7960) // 0x00880360
OPENSC4_JUMP1(PushString, void, const char*, 0, addr::cSCLua_F8B79B0) // 0x00880370
OPENSC4_JUMP2(PushCClosure, void, void*, int32_t, 0, addr::cSCLua_F8B7A00) // 0x00880380
OPENSC4_JUMP2(PushUserTag, void, void*, int32_t, 0, addr::cSCLua_F8B87A0) // 0x00880390
OPENSC4_JUMP1(PushBoolean, void, uint8_t, 0, addr::cSCLua_F8B7A50) // 0x008803A0
OPENSC4_JUMP1(GetGlobal, void, const char*, 0, addr::cSCLua_F8B8280) // 0x008803B0
OPENSC4_JUMP1(GetTable, void, int32_t, 0, addr::cSCLua_F8B7B40) // 0x008803C0
OPENSC4_JUMP1(RawGet, void, int32_t, 0, addr::cSCLua_F8B7B90) // 0x008803D0
OPENSC4_JUMP2(RawGetI, void, int32_t, int32_t, 0, addr::cSCLua_F8B7BE0) // 0x008803E0
OPENSC4_JUMP2(GetTagMethod, void, int32_t, const char*, 0, addr::cSCLua_F8B84B0) // 0x00880400
OPENSC4_JUMP1(GetRef, void, int32_t, 0, addr::cSCLua_F8B8390) // 0x00880410
OPENSC4_JUMP1(SetGlobal, void, const char*, 0, addr::cSCLua_F8B82E0) // 0x00880430
OPENSC4_JUMP1(SetTable, void, int32_t, 0, addr::cSCLua_F8B7DB0) // 0x00880440
OPENSC4_JUMP1(RawSet, void, int32_t, 0, addr::cSCLua_F8B7E00) // 0x00880450
OPENSC4_JUMP2(RawSetI, void, int32_t, int32_t, 0, addr::cSCLua_F8B7E50) // 0x00880460
OPENSC4_JUMP2(SetTagMethod, void, int32_t, const char*, 0, addr::cSCLua_F8B8540) // 0x00880470
OPENSC4_JUMP1(DoFile, int32_t, const char*, 0, addr::cSCLua_F8B7F90) // 0x008804C0
OPENSC4_JUMP1(DoString, int32_t, const char*, 0, addr::cSCLua_F8B7FE0) // 0x008804D0
OPENSC4_JUMP3(DoBuffer, int32_t, const char*, uint32_t, const char*, 0, addr::cSCLua_F8B8030) // 0x008804E0
OPENSC4_JUMP0(NewTag, int32_t, 0, addr::cSCLua_F8B85E0) // 0x00880520
OPENSC4_JUMP2(CopyTagMethods, int32_t, int32_t, int32_t, 0, addr::cSCLua_F8B86C0) // 0x00880530
OPENSC4_JUMP1(SetTag, void, int32_t, 0, addr::cSCLua_F8B8670) // 0x00880540
OPENSC4_JUMP1(Unref, void, int32_t, 0, addr::cSCLua_F8B83E0) // 0x00880550
OPENSC4_JUMP1(Next, int32_t, int32_t, 0, addr::cSCLua_F8B8130) // 0x00880560
OPENSC4_JUMP1(Error, void, const char*, 0, addr::cSCLua_F8B80F0) // 0x00880620
OPENSC4_JUMP1(Concat, void, int32_t, 0, addr::cSCLua_F8B8180) // 0x00880630
OPENSC4_JUMP2(Register, void, void*, const char*, 0, addr::cSCLua_F8B81D0) // 0x00880650
OPENSC4_JUMP1(CloneTag, int32_t, int32_t, 0, addr::cSCLua_F8B8750) // 0x00880670
OPENSC4_JUMP1(IsFunction, uint8_t, int32_t, 0, addr::cSCLua_F8B7240) // 0x00880680
OPENSC4_JUMP1(IsTable, uint8_t, int32_t, 0, addr::cSCLua_F8B72A0) // 0x00880690
OPENSC4_JUMP1(IsUserData, uint8_t, int32_t, 0, addr::cSCLua_F8B7300) // 0x008806A0
OPENSC4_JUMP1(IsNil, uint8_t, int32_t, 0, addr::cSCLua_F8B7410) // 0x008806B0
} // namespace thread

namespace engine {
OPENSC4_JUMP0(GetGCCount, int32_t, 0, addr::cSCLua_F8B8C20) // 0x008804F0
OPENSC4_JUMP0(GetGCThreshold, int32_t, 0, addr::cSCLua_F8B8C60) // 0x00880500
OPENSC4_JUMP1(SetGCThreshold, void, int32_t, 0, addr::cSCLua_F8B8CA0) // 0x00880510
} // namespace engine

namespace script {
OPENSC4_JUMP0(GetGlobals, void, -0x38, addr::cSCLua_F8B7D60) // 0x008803F0
OPENSC4_JUMP0(NewTable, void, -0x38, addr::cSCLua_F8B7C30) // 0x00880420
OPENSC4_JUMP2(RawCall, void, int32_t, int32_t, -0x38, addr::cSCLua_F8B7F40) // 0x008804B0
OPENSC4_JUMP1(NewUserData, void*, uint32_t, -0x38, addr::cSCLua_F8B7AF0) // 0x00880640
OPENSC4_JUMP2(PushUserData, void, void*, int32_t, -0x38, addr::cSCLua_F8B87A0) // 0x00880660
} // namespace script

namespace thread {

// The game's 0x00880340: the number is pushed again for the base's function
// (a call, not a jump).
void __fastcall PushNumber(char* self, void*, double value)
{
    This<void>(addr::cSCLua_F8B7910, self, value);
}

} // namespace thread

namespace script {

// The game's 0x00880480, the interface's Ref: the argument is not used; the
// base makes a reference to the value on top of the stack.
int32_t __fastcall Ref(char* self, void*, int32_t)
{
    return This<int32_t>(addr::cSCLua_F8B8340, self - 0x38);
}

// The game's 0x00880490, the interface's CallProtected.
int32_t __fastcall CallProtected(char* self, void*, int32_t arguments, int32_t results)
{
    return This<int32_t>(addr::cSCLua_F8B8F30, self - 0x38, arguments, results, int32_t{0}, uint32_t{1});
}

// The game's 0x00880570, the interface's GetN: the script's own `getn` of
// the table. Where the value is a table but `getn` is no function or its
// call fails, the result is the index itself.
int32_t __fastcall GetN(char* self, void*, int32_t index)
{
    char* const thread = self - 0x38;
    if (!This<uint8_t>(addr::cSCLua_F8B72A0, thread, index))
        return 0;
    const int32_t top = This<int32_t>(addr::cSCLua_F8B6DF0, thread);
    This<void>(addr::cSCLua_F8B6E80, thread, index);
    This<void>(addr::cSCLua_F8B6E80, thread, int32_t{-1});
    This<void>(addr::cSCLua_F8B8280, thread, "getn");
    if (This<uint8_t>(addr::cSCLua_F8B7240, thread, int32_t{-1})) {
        This<void>(addr::cSCLua_F8B6FC0, thread, int32_t{-3});
        if (This<int32_t>(addr::cSCLua_F8B8F30, thread, int32_t{1}, int32_t{1}, int32_t{0}, uint32_t{1}) == 0) {
            const int32_t count = Ftol(This<double>(addr::cSCLua_F8B7690, thread, int32_t{-1}));
            This<void>(addr::cSCLua_F8B6E30, thread, top);
            return count;
        }
    }
    This<void>(addr::cSCLua_F8B6E30, thread, top);
    return index;
}

} // namespace script

// ---------------------------------------------------------------------------
// The context's own functions (0x008806C0 to 0x00880A60).

// The game's 0x008806C0. `this` is the object. The base answers for every
// interface but the script's.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    *object = nullptr;
    if (iid != kScriptIid)
        return This<uint8_t>(addr::cSCLua_F8B8CF0, self, iid, object);
    *object = self ? self + part::kScript : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x00880710 (cdecl): the callback of a walk of the data registry
// when it goes away; `context` is a pair {the engine part, the registry}.
// The table the script kept for a node entry is dropped from Lua's registry.
uint8_t __cdecl ForgetRegistryNode(void* entry, void** context)
{
    char* const thread = Virtual<char*>(context[0], 0xC);
    if (Virtual<uint8_t>(context[1], 0x34, entry)) {
        Virtual<void>(thread, 0xD8, entry);
        Virtual<void>(thread, 0xC0);
        Virtual<void>(thread, 0x104, kRegistryIndex);
    }
    return 1;
}

namespace script {

// The game's 0x00880760, the interface's ToDataRegistryEntryID: the entry in
// the table's "_e" field, if the registry knows it. A negative index is made
// absolute by adding the stack's height (not the height plus one).
void* __fastcall ToDataRegistryEntryId(char* self, void*, void* registry, int32_t index)
{
    char* const thread = self - 0x38;
    void* result = nullptr;
    if (!Virtual<uint8_t>(thread, 0x74, index))
        return result;
    const int32_t top = This<int32_t>(addr::cSCLua_F8B6DF0, thread);
    if (index < 0)
        index += top;
    This<void>(addr::cSCLua_F8B79B0, thread, Global<const char*>(addr::cSCLua_EntryFieldName));
    This<void>(addr::cSCLua_F8B7B90, thread, index);
    void* const entry = This<void*>(addr::cSCLua_F8B7830, thread, int32_t{-1});
    if (entry && Virtual<uint8_t>(registry, 0x30, entry))
        result = entry;
    This<void>(addr::cSCLua_F8B6E30, thread, top);
    return result;
}

// The game's 0x008807D0, the interface's ToDataRegistryNode.
void* __fastcall ToDataRegistryNode(char* self, void*, void* registry, int32_t index)
{
    void* const entry = Virtual<void*>(self, 0x12C, registry, index);
    if (registry && Virtual<uint8_t>(registry, 0x30, entry) && Virtual<uint8_t>(registry, 0x34, entry))
        return Virtual<void*>(registry, 0x28, entry);
    return nullptr;
}

// The game's 0x00880820, the interface's ToDataRegistryValue.
void* __fastcall ToDataRegistryValue(char* self, void*, void* registry, int32_t index)
{
    void* const entry = Virtual<void*>(self, 0x12C, registry, index);
    if (registry && Virtual<uint8_t>(registry, 0x30, entry))
        return Virtual<void*>(registry, 0x2C, entry);
    return nullptr;
}

} // namespace script

// A new parameter set (0x28 bytes), or null.
char* NewParameterSet()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
    return memory ? This<char*>(addr::SC4ScriptParameterSet_Construct, memory) : nullptr;
}

// Puts `fresh` in a counted field.
void ReplaceCounted(char*& slot, char* fresh)
{
    char* const old = slot;
    if (fresh == old)
        return;
    if (fresh)
        Virtual<uint32_t>(fresh, 0x4);
    slot = fresh;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// The service the context's notification target is added to and removed
// from (ID 0xEBE0E860, interface 0xE898ED03). Without a framework the
// reference stays null and the caller's call through it faults, as in the
// original.
void GetNotifier(void** service)
{
    *service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xEBE0E860}, uint32_t{0xE898ED03}, service);
}

namespace engine {

// The game's 0x00880860. `this` is the engine part (+0x1C).
uint8_t __fastcall Open(char* self, void*, uint32_t flags)
{
    char* const object = self - part::kEngine;
    if (!At<uint8_t>(object, field::kOpened)) {
        At<uint8_t>(object, field::kOpened) = This<uint8_t>(addr::cSCLua_F8B8890, self, flags);
        ReplaceCounted(At<char*>(object, field::kInput), NewParameterSet());
        ReplaceCounted(At<char*>(object, field::kOutput), NewParameterSet());
        void* const target = object ? object + part::kTarget : nullptr;
        void* service;
        GetNotifier(&service);
        Virtual<uint8_t>(service, 0x64, target);
        This<void>(addr::SC4_ReleaseRefHolder, &service);
    }
    return At<uint8_t>(object, field::kOpened);
}

// The game's 0x00880950. `this` is the engine part (+0x1C).
uint8_t __fastcall Close(char* self, void*)
{
    char* const object = self - part::kEngine;
    if (At<uint8_t>(object, field::kOpened)) {
        void* const target = object ? object + part::kTarget : nullptr;
        void* service;
        GetNotifier(&service);
        Virtual<uint8_t>(service, 0x6C, target);
        This<void>(addr::SC4_ReleaseRefHolder, &service);
        At<uint8_t>(object, field::kOpened) = 0;
        This<uint8_t>(addr::cSCLua_F8B89B0, self);
        for (const std::size_t offset : {field::kInput, field::kOutput}) {
            if (char* const held = At<char*>(object, offset)) {
                At<char*>(object, offset) = nullptr;
                Virtual<uint32_t>(held, 0x8);
            }
        }
    }
    return 1;
}

// The game's 0x00880A40: the engine part's thread.
char* __fastcall AsThread(char* self, void*)
{
    char* const object = self - part::kEngine;
    return object ? object + part::kThread : nullptr;
}

} // namespace engine

namespace script {

// The game's 0x00880A50: the script interface's engine part.
char* __fastcall AsEngine(char* self, void*)
{
    char* const object = self - part::kScript;
    return object ? object + part::kEngine : nullptr;
}

} // namespace script

// The game's 0x008809E0: the constructor. `this` is the object. The
// abstract interfaces' vtables are written first, as the original does.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::cSCLua_F8B9670, self);
    At<std::uintptr_t>(self, part::kScript) = addr::cSCLua_AbstractScriptVtable;
    At<std::uintptr_t>(self, part::kTarget) = addr::cSCLua_AbstractTargetVtable;
    At<std::uintptr_t>(self, 0x0) = addr::cSCLua_Vtable;
    At<std::uintptr_t>(self, part::kThread) = addr::cSCLua_ThreadVtable;
    At<std::uintptr_t>(self, part::kEngine) = addr::cSCLua_EngineVtable;
    At<std::uintptr_t>(self, part::kScript) = addr::cSCLua_ScriptVtable;
    At<std::uintptr_t>(self, part::kTarget) = addr::cSCLua_TargetVtable;
    At<uint8_t>(self, field::kOpened) = 0;
    At<char*>(self, field::kInput) = nullptr;
    At<char*>(self, field::kOutput) = nullptr;
    At<char*>(self, field::kList) = nullptr;
    char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0xC});
    At<char*>(node, 0x0) = node;
    At<char*>(node, 0x4) = node;
    At<char*>(self, field::kList) = node;
    return self;
}

// The game's 0x00880EE0: the destructor. `this` is the object.
void __fastcall Destroy(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSCLua_Vtable;
    At<std::uintptr_t>(self, part::kThread) = addr::cSCLua_ThreadVtable;
    At<std::uintptr_t>(self, part::kEngine) = addr::cSCLua_EngineVtable;
    At<std::uintptr_t>(self, part::kScript) = addr::cSCLua_ScriptVtable;
    At<std::uintptr_t>(self, part::kTarget) = addr::cSCLua_TargetVtable;
    This<void>(addr::SC4PairList_Destroy, self + field::kList);
    if (char* const output = At<char*>(self, field::kOutput))
        Virtual<uint32_t>(output, 0x8);
    if (char* const input = At<char*>(self, field::kInput))
        Virtual<uint32_t>(input, 0x8);
    This<void>(addr::cSCLua_F8B9240, self);
}

// The game's 0x00880F30: the deleting destructor.
char* __fastcall Delete(char* self, void*, uint32_t flags)
{
    Destroy(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ---------------------------------------------------------------------------
// Second-level thunks (0x00880A60 to 0x00880EE0): the slots of the script
// interface (+0x40) in `script`, then the QueryInterface, AddRef and Release
// of each part, which go back to the object.

namespace script {
OPENSC4_VIA0(GetTop, int32_t, -0x38, thread::GetTop) // 0x00880AD0
OPENSC4_VIA1(SetTop, void, int32_t, -0x38, thread::SetTop) // 0x00880AE0
OPENSC4_VIA1(PushValue, void, int32_t, -0x38, thread::PushValue) // 0x00880AF0
OPENSC4_VIA1(Remove, void, int32_t, -0x38, thread::Remove) // 0x00880B00
OPENSC4_VIA1(Insert, void, int32_t, -0x38, thread::Insert) // 0x00880B10
OPENSC4_VIA1(Pop, void, int32_t, -0x38, thread::Pop) // 0x00880B20
OPENSC4_VIA1(Type, int32_t, int32_t, -0x38, thread::Type) // 0x00880B30
OPENSC4_VIA1(TypeName, const char*, int32_t, -0x38, thread::TypeName) // 0x00880B40
OPENSC4_VIA1(IsNumber, uint8_t, int32_t, -0x38, thread::IsNumber) // 0x00880B50
OPENSC4_VIA1(IsString, uint8_t, int32_t, -0x38, thread::IsString) // 0x00880B60
OPENSC4_VIA1(IsCFunction, uint8_t, int32_t, -0x38, thread::IsCFunction) // 0x00880B70
OPENSC4_VIA1(Tag, int32_t, int32_t, -0x38, thread::Tag) // 0x00880B80
OPENSC4_VIA1(IsBoolean, uint8_t, int32_t, -0x38, thread::IsBoolean) // 0x00880B90
OPENSC4_VIA2(Equal, uint8_t, int32_t, int32_t, -0x38, thread::Equal) // 0x00880BA0
OPENSC4_VIA2(LessThan, uint8_t, int32_t, int32_t, -0x38, thread::LessThan) // 0x00880BB0
OPENSC4_VIA1(ToInteger, int32_t, int32_t, -0x38, thread::ToInteger) // 0x00880BC0
OPENSC4_VIA1(ToNumber, double, int32_t, -0x38, thread::ToNumber) // 0x00880BD0
OPENSC4_VIA1(ToString, const char*, int32_t, -0x38, thread::ToString) // 0x00880BE0
OPENSC4_VIA1(Strlen, uint32_t, int32_t, -0x38, thread::Strlen) // 0x00880BF0
OPENSC4_VIA1(ToCFunction, void*, int32_t, -0x38, thread::ToCFunction) // 0x00880C00
OPENSC4_VIA1(ToUserData, void*, int32_t, -0x38, thread::ToUserData) // 0x00880C10
OPENSC4_VIA1(ToUserTag, uint32_t, int32_t, -0x38, thread::ToUserTag) // 0x00880C20
OPENSC4_VIA1(ToBoolean, uint8_t, int32_t, -0x38, thread::ToBoolean) // 0x00880C30
OPENSC4_VIA0(PushNil, void, -0x38, thread::PushNil) // 0x00880C40
OPENSC4_VIA1(PushNumber, void, double, -0x38, thread::PushNumber) // 0x00880C50
OPENSC4_VIA2(PushLString, void, const char*, uint32_t, -0x38, thread::PushLString) // 0x00880C60
OPENSC4_VIA1(PushString, void, const char*, -0x38, thread::PushString) // 0x00880C70
OPENSC4_VIA2(PushCClosure, void, void*, int32_t, -0x38, thread::PushCClosure) // 0x00880C80
OPENSC4_VIA2(PushUserTag, void, void*, int32_t, -0x38, thread::PushUserTag) // 0x00880C90
OPENSC4_VIA1(PushBoolean, void, uint8_t, -0x38, thread::PushBoolean) // 0x00880CA0
OPENSC4_VIA1(GetGlobal, void, const char*, -0x38, thread::GetGlobal) // 0x00880CB0
OPENSC4_VIA1(GetTable, void, int32_t, -0x38, thread::GetTable) // 0x00880CC0
OPENSC4_VIA1(RawGet, void, int32_t, -0x38, thread::RawGet) // 0x00880CD0
OPENSC4_VIA2(RawGetI, void, int32_t, int32_t, -0x38, thread::RawGetI) // 0x00880CE0
OPENSC4_VIA2(GetTagMethod, void, int32_t, const char*, -0x38, thread::GetTagMethod) // 0x00880CF0
OPENSC4_VIA1(GetRef, void, int32_t, -0x38, thread::GetRef) // 0x00880D00
OPENSC4_VIA1(SetGlobal, void, const char*, -0x38, thread::SetGlobal) // 0x00880D10
OPENSC4_VIA1(SetTable, void, int32_t, -0x38, thread::SetTable) // 0x00880D20
OPENSC4_VIA1(RawSet, void, int32_t, -0x38, thread::RawSet) // 0x00880D30
OPENSC4_VIA2(RawSetI, void, int32_t, int32_t, -0x38, thread::RawSetI) // 0x00880D40
OPENSC4_VIA2(SetTagMethod, void, int32_t, const char*, -0x38, thread::SetTagMethod) // 0x00880D50
OPENSC4_VIA1(DoFile, int32_t, const char*, -0x38, thread::DoFile) // 0x00880D60
OPENSC4_VIA1(DoString, int32_t, const char*, -0x38, thread::DoString) // 0x00880D70
OPENSC4_VIA3(DoBuffer, int32_t, const char*, uint32_t, const char*, -0x38, thread::DoBuffer) // 0x00880D80
OPENSC4_VIA0(GetGCCount, int32_t, -0x24, engine::GetGCCount) // 0x00880D90
OPENSC4_VIA0(GetGCThreshold, int32_t, -0x24, engine::GetGCThreshold) // 0x00880DA0
OPENSC4_VIA1(SetGCThreshold, void, int32_t, -0x24, engine::SetGCThreshold) // 0x00880DB0
OPENSC4_VIA0(NewTag, int32_t, -0x38, thread::NewTag) // 0x00880DC0
OPENSC4_VIA2(CopyTagMethods, int32_t, int32_t, int32_t, -0x38, thread::CopyTagMethods) // 0x00880DD0
OPENSC4_VIA1(SetTag, void, int32_t, -0x38, thread::SetTag) // 0x00880DE0
OPENSC4_VIA1(Unref, void, int32_t, -0x38, thread::Unref) // 0x00880DF0
OPENSC4_VIA1(Next, int32_t, int32_t, -0x38, thread::Next) // 0x00880E00
OPENSC4_VIA1(Error, void, const char*, -0x38, thread::Error) // 0x00880E10
OPENSC4_VIA1(Concat, void, int32_t, -0x38, thread::Concat) // 0x00880E20
OPENSC4_VIA2(Register, void, void*, const char*, -0x38, thread::Register) // 0x00880E30
OPENSC4_VIA1(CloneTag, int32_t, int32_t, -0x38, thread::CloneTag) // 0x00880E40
OPENSC4_VIA1(IsFunction, uint8_t, int32_t, -0x38, thread::IsFunction) // 0x00880E50
OPENSC4_VIA1(IsTable, uint8_t, int32_t, -0x38, thread::IsTable) // 0x00880E60
OPENSC4_VIA1(IsUserData, uint8_t, int32_t, -0x38, thread::IsUserData) // 0x00880E70
OPENSC4_VIA1(IsNil, uint8_t, int32_t, -0x38, thread::IsNil) // 0x00880E80
OPENSC4_VIA1(Open, uint8_t, uint32_t, -0x24, engine::Open) // 0x00880EC0
OPENSC4_VIA0(Close, uint8_t, -0x24, engine::Close) // 0x00880ED0
} // namespace script

OPENSC4_VIA2(QueryInterfaceOfThread, uint8_t, uint32_t, void**, -0x8, QueryInterface) // 0x00880A60
OPENSC4_VIA2(QueryInterfaceOfEngine, uint8_t, uint32_t, void**, -0x1C, QueryInterface) // 0x00880A70
OPENSC4_JUMP0(AddRefOfEngine, uint32_t, -0x1C, addr::cRZUnknown_AddRefThunk) // 0x00880A80
OPENSC4_JUMP0(ReleaseOfEngine, uint32_t, -0x1C, addr::cRZUnknown_ReleaseThunk) // 0x00880A90
OPENSC4_VIA2(QueryInterfaceOfScript, uint8_t, uint32_t, void**, -0x40, QueryInterface) // 0x00880AA0
OPENSC4_JUMP0(AddRefOfScript, uint32_t, -0x40, addr::cRZUnknown_AddRefThunk) // 0x00880AB0
OPENSC4_JUMP0(ReleaseOfScript, uint32_t, -0x40, addr::cRZUnknown_ReleaseThunk) // 0x00880AC0
OPENSC4_VIA2(QueryInterfaceOfTarget, uint8_t, uint32_t, void**, -0x44, QueryInterface) // 0x00880E90
OPENSC4_JUMP0(AddRefOfTarget, uint32_t, -0x44, addr::cRZUnknown_AddRefThunk) // 0x00880EA0
OPENSC4_JUMP0(ReleaseOfTarget, uint32_t, -0x44, addr::cRZUnknown_ReleaseThunk) // 0x00880EB0

#undef OPENSC4_JUMP0
#undef OPENSC4_JUMP1
#undef OPENSC4_JUMP2
#undef OPENSC4_JUMP3
#undef OPENSC4_VIA0
#undef OPENSC4_VIA1
#undef OPENSC4_VIA2
#undef OPENSC4_VIA3

// ---------------------------------------------------------------------------
// The game's 0x00880F50 (cdecl, a Lua closure): the function behind a value
// entry of the data registry in a script. Its three upvalues are the
// registry, the entry and the script context that made it. Called with an
// argument it gives the entry that value; called without, it pushes the
// entry's value. One result either way.
int32_t __cdecl RegistryValueExchange(char* state)
{
    char* const thread = *reinterpret_cast<char**>(state - 4);
    const auto upvalue = [thread](int32_t number) {
        return Virtual<char*>(thread, 0xB8, Virtual<int32_t>(thread, 0x148, number));
    };
    char* const registry = upvalue(1);
    char* const entry = upvalue(2);
    char* const context = upvalue(3);
    if (!Virtual<uint8_t>(registry, 0x30, entry))
        return 1;

    const uint8_t set = Virtual<int32_t>(thread, 0x38) > 0;
    ScriptValue value;
    This<void*>(addr::SC4ScriptNumber_Construct, &value);
    if (set) {
        switch (Virtual<int32_t>(thread, 0x5C, int32_t{1})) {
        case kLuaString: {
            GameString text;
            void* const made =
                This<void*>(addr::SC4String_FromText, &text, Virtual<const char*>(thread, 0xAC, int32_t{1}));
            This<void>(addr::SC4ScriptNumber_SetString, &value, made);
            This<void>(addr::SC4String_Destroy, &text);
            break;
        }
        case kLuaBoolean:
            This<void>(addr::SC4ScriptNumber_SetBool, &value, Virtual<uint8_t>(thread, 0xA8, int32_t{1}));
            break;
        case kLuaNumber:
            This<void>(addr::SC4ScriptNumber_SetDouble, &value, Virtual<double>(thread, 0xA4, int32_t{1}));
            break;
        case kLuaTable: {
            // A table stands for a node of the registry: its "_e" field.
            void* node = nullptr;
            Virtual<void>(thread, 0xCC, Global<const char*>(addr::cSCLua_EntryFieldName));
            Virtual<void>(thread, 0xE8, int32_t{1});
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
    }

    Virtual<uint8_t>(registry, 0x3C, entry, &value, set);
    if (!set) {
        const uint32_t type = value.type;
        GameString text;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        text.begin = buffer;
        text.end = buffer;
        text.capacity = buffer + 8;
        *buffer = 0;
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        if (type == 1) {
            Virtual<void>(thread, 0xD4, This<uint8_t>(addr::SC4ScriptNumber_GetBool, &value));
        } else if (type >= 2 && type <= 14) {
            Virtual<void>(thread, 0xC4, This<double>(addr::SC4ScriptNumber_GetDouble, &value));
        } else if (type == 16) {
            void* const node = Virtual<void*>(registry, 0x24, This<void*>(addr::SC4ScriptNumber_GetObject, &value));
            if (node) {
                // The node's table, kept in the registry of the context that
                // made this closure, moved to the calling state.
                char* const owner = context + part::kThread;
                Virtual<void>(owner, 0xD8, node);
                Virtual<void>(owner, 0xE8, kRegistryIndex);
                Virtual<void>(owner, 0x54, state, int32_t{1});
            } else {
                Virtual<void>(thread, 0xC0);
            }
        } else {
            This<uint8_t>(addr::SC4ScriptNumber_GetString, &value, &text);
            Virtual<void>(thread, 0xCC, This<const char*>(addr::cRZBaseString_Data, &text));
        }
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            Cdecl<void>(addr::SC4_Free, text.begin);
    }
    This<void>(addr::SC4ScriptNumber_Destroy, &value);
    return 1;
}

bool InstallPart1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSCLua_F87FFF0, reinterpret_cast<void*>(&DirectorId)},
        {addr::cSCLua_F880000, reinterpret_cast<void*>(&CreateClass2BD13FB4)},
        {addr::cSCLua_F880050, reinterpret_cast<void*>(&CreateThread)},
        {addr::cSCLua_F8800A0, reinterpret_cast<void*>(&CreateContext)},
        {addr::cSCLua_F8800F0, reinterpret_cast<void*>(&DirectorConstruct)},
        {addr::cSCLua_F880150, reinterpret_cast<void*>(&DirectorDelete)},
        {addr::cSCLua_F880180, reinterpret_cast<void*>(&GetDirector)},
        {addr::cSCLua_F880340, reinterpret_cast<void*>(&thread::PushNumber)},
        {addr::cSCLua_F880480, reinterpret_cast<void*>(&script::Ref)},
        {addr::cSCLua_F880490, reinterpret_cast<void*>(&script::CallProtected)},
        {addr::cSCLua_F880570, reinterpret_cast<void*>(&script::GetN)},
        {addr::cSCLua_F8806C0, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSCLua_F880710, reinterpret_cast<void*>(&ForgetRegistryNode)},
        {addr::cSCLua_F880760, reinterpret_cast<void*>(&script::ToDataRegistryEntryId)},
        {addr::cSCLua_F8807D0, reinterpret_cast<void*>(&script::ToDataRegistryNode)},
        {addr::cSCLua_F880820, reinterpret_cast<void*>(&script::ToDataRegistryValue)},
        {addr::cSCLua_F880860, reinterpret_cast<void*>(&engine::Open)},
        {addr::cSCLua_F880950, reinterpret_cast<void*>(&engine::Close)},
        {addr::cSCLua_F8809E0, reinterpret_cast<void*>(&Construct)},
        {addr::cSCLua_F880A40, reinterpret_cast<void*>(&engine::AsThread)},
        {addr::cSCLua_F880A50, reinterpret_cast<void*>(&script::AsEngine)},
        {addr::cSCLua_F880EE0, reinterpret_cast<void*>(&Destroy)},
        {addr::cSCLua_F880F30, reinterpret_cast<void*>(&Delete)},
        {addr::cSCLua_F880F50, reinterpret_cast<void*>(&RegistryValueExchange)},
        // The thunks, by address.
        {addr::cSCLua_F8801C0, reinterpret_cast<void*>(&thread::GetTop)},
        {addr::cSCLua_F8801D0, reinterpret_cast<void*>(&thread::SetTop)},
        {addr::cSCLua_F8801E0, reinterpret_cast<void*>(&thread::PushValue)},
        {addr::cSCLua_F8801F0, reinterpret_cast<void*>(&thread::Remove)},
        {addr::cSCLua_F880200, reinterpret_cast<void*>(&thread::Insert)},
        {addr::cSCLua_F880210, reinterpret_cast<void*>(&thread::Pop)},
        {addr::cSCLua_F880220, reinterpret_cast<void*>(&thread::Type)},
        {addr::cSCLua_F880230, reinterpret_cast<void*>(&thread::TypeName)},
        {addr::cSCLua_F880240, reinterpret_cast<void*>(&thread::IsNumber)},
        {addr::cSCLua_F880250, reinterpret_cast<void*>(&thread::IsBoolean)},
        {addr::cSCLua_F880260, reinterpret_cast<void*>(&thread::IsString)},
        {addr::cSCLua_F880270, reinterpret_cast<void*>(&thread::IsCFunction)},
        {addr::cSCLua_F880280, reinterpret_cast<void*>(&thread::Tag)},
        {addr::cSCLua_F880290, reinterpret_cast<void*>(&thread::Equal)},
        {addr::cSCLua_F8802A0, reinterpret_cast<void*>(&thread::LessThan)},
        {addr::cSCLua_F8802B0, reinterpret_cast<void*>(&thread::ToInteger)},
        {addr::cSCLua_F8802C0, reinterpret_cast<void*>(&thread::ToNumber)},
        {addr::cSCLua_F8802D0, reinterpret_cast<void*>(&thread::ToString)},
        {addr::cSCLua_F8802E0, reinterpret_cast<void*>(&thread::Strlen)},
        {addr::cSCLua_F8802F0, reinterpret_cast<void*>(&thread::ToCFunction)},
        {addr::cSCLua_F880300, reinterpret_cast<void*>(&thread::ToUserData)},
        {addr::cSCLua_F880310, reinterpret_cast<void*>(&thread::ToUserTag)},
        {addr::cSCLua_F880320, reinterpret_cast<void*>(&thread::ToBoolean)},
        {addr::cSCLua_F880330, reinterpret_cast<void*>(&thread::PushNil)},
        {addr::cSCLua_F880360, reinterpret_cast<void*>(&thread::PushLString)},
        {addr::cSCLua_F880370, reinterpret_cast<void*>(&thread::PushString)},
        {addr::cSCLua_F880380, reinterpret_cast<void*>(&thread::PushCClosure)},
        {addr::cSCLua_F880390, reinterpret_cast<void*>(&thread::PushUserTag)},
        {addr::cSCLua_F8803A0, reinterpret_cast<void*>(&thread::PushBoolean)},
        {addr::cSCLua_F8803B0, reinterpret_cast<void*>(&thread::GetGlobal)},
        {addr::cSCLua_F8803C0, reinterpret_cast<void*>(&thread::GetTable)},
        {addr::cSCLua_F8803D0, reinterpret_cast<void*>(&thread::RawGet)},
        {addr::cSCLua_F8803E0, reinterpret_cast<void*>(&thread::RawGetI)},
        {addr::cSCLua_F8803F0, reinterpret_cast<void*>(&script::GetGlobals)},
        {addr::cSCLua_F880400, reinterpret_cast<void*>(&thread::GetTagMethod)},
        {addr::cSCLua_F880410, reinterpret_cast<void*>(&thread::GetRef)},
        {addr::cSCLua_F880420, reinterpret_cast<void*>(&script::NewTable)},
        {addr::cSCLua_F880430, reinterpret_cast<void*>(&thread::SetGlobal)},
        {addr::cSCLua_F880440, reinterpret_cast<void*>(&thread::SetTable)},
        {addr::cSCLua_F880450, reinterpret_cast<void*>(&thread::RawSet)},
        {addr::cSCLua_F880460, reinterpret_cast<void*>(&thread::RawSetI)},
        {addr::cSCLua_F880470, reinterpret_cast<void*>(&thread::SetTagMethod)},
        {addr::cSCLua_F8804B0, reinterpret_cast<void*>(&script::RawCall)},
        {addr::cSCLua_F8804C0, reinterpret_cast<void*>(&thread::DoFile)},
        {addr::cSCLua_F8804D0, reinterpret_cast<void*>(&thread::DoString)},
        {addr::cSCLua_F8804E0, reinterpret_cast<void*>(&thread::DoBuffer)},
        {addr::cSCLua_F8804F0, reinterpret_cast<void*>(&engine::GetGCCount)},
        {addr::cSCLua_F880500, reinterpret_cast<void*>(&engine::GetGCThreshold)},
        {addr::cSCLua_F880510, reinterpret_cast<void*>(&engine::SetGCThreshold)},
        {addr::cSCLua_F880520, reinterpret_cast<void*>(&thread::NewTag)},
        {addr::cSCLua_F880530, reinterpret_cast<void*>(&thread::CopyTagMethods)},
        {addr::cSCLua_F880540, reinterpret_cast<void*>(&thread::SetTag)},
        {addr::cSCLua_F880550, reinterpret_cast<void*>(&thread::Unref)},
        {addr::cSCLua_F880560, reinterpret_cast<void*>(&thread::Next)},
        {addr::cSCLua_F880620, reinterpret_cast<void*>(&thread::Error)},
        {addr::cSCLua_F880630, reinterpret_cast<void*>(&thread::Concat)},
        {addr::cSCLua_F880640, reinterpret_cast<void*>(&script::NewUserData)},
        {addr::cSCLua_F880650, reinterpret_cast<void*>(&thread::Register)},
        {addr::cSCLua_F880660, reinterpret_cast<void*>(&script::PushUserData)},
        {addr::cSCLua_F880670, reinterpret_cast<void*>(&thread::CloneTag)},
        {addr::cSCLua_F880680, reinterpret_cast<void*>(&thread::IsFunction)},
        {addr::cSCLua_F880690, reinterpret_cast<void*>(&thread::IsTable)},
        {addr::cSCLua_F8806A0, reinterpret_cast<void*>(&thread::IsUserData)},
        {addr::cSCLua_F8806B0, reinterpret_cast<void*>(&thread::IsNil)},
        {addr::cSCLua_F880A60, reinterpret_cast<void*>(&QueryInterfaceOfThread)},
        {addr::cSCLua_F880A70, reinterpret_cast<void*>(&QueryInterfaceOfEngine)},
        {addr::cSCLua_F880A80, reinterpret_cast<void*>(&AddRefOfEngine)},
        {addr::cSCLua_F880A90, reinterpret_cast<void*>(&ReleaseOfEngine)},
        {addr::cSCLua_F880AA0, reinterpret_cast<void*>(&QueryInterfaceOfScript)},
        {addr::cSCLua_F880AB0, reinterpret_cast<void*>(&AddRefOfScript)},
        {addr::cSCLua_F880AC0, reinterpret_cast<void*>(&ReleaseOfScript)},
        {addr::cSCLua_F880AD0, reinterpret_cast<void*>(&script::GetTop)},
        {addr::cSCLua_F880AE0, reinterpret_cast<void*>(&script::SetTop)},
        {addr::cSCLua_F880AF0, reinterpret_cast<void*>(&script::PushValue)},
        {addr::cSCLua_F880B00, reinterpret_cast<void*>(&script::Remove)},
        {addr::cSCLua_F880B10, reinterpret_cast<void*>(&script::Insert)},
        {addr::cSCLua_F880B20, reinterpret_cast<void*>(&script::Pop)},
        {addr::cSCLua_F880B30, reinterpret_cast<void*>(&script::Type)},
        {addr::cSCLua_F880B40, reinterpret_cast<void*>(&script::TypeName)},
        {addr::cSCLua_F880B50, reinterpret_cast<void*>(&script::IsNumber)},
        {addr::cSCLua_F880B60, reinterpret_cast<void*>(&script::IsString)},
        {addr::cSCLua_F880B70, reinterpret_cast<void*>(&script::IsCFunction)},
        {addr::cSCLua_F880B80, reinterpret_cast<void*>(&script::Tag)},
        {addr::cSCLua_F880B90, reinterpret_cast<void*>(&script::IsBoolean)},
        {addr::cSCLua_F880BA0, reinterpret_cast<void*>(&script::Equal)},
        {addr::cSCLua_F880BB0, reinterpret_cast<void*>(&script::LessThan)},
        {addr::cSCLua_F880BC0, reinterpret_cast<void*>(&script::ToInteger)},
        {addr::cSCLua_F880BD0, reinterpret_cast<void*>(&script::ToNumber)},
        {addr::cSCLua_F880BE0, reinterpret_cast<void*>(&script::ToString)},
        {addr::cSCLua_F880BF0, reinterpret_cast<void*>(&script::Strlen)},
        {addr::cSCLua_F880C00, reinterpret_cast<void*>(&script::ToCFunction)},
        {addr::cSCLua_F880C10, reinterpret_cast<void*>(&script::ToUserData)},
        {addr::cSCLua_F880C20, reinterpret_cast<void*>(&script::ToUserTag)},
        {addr::cSCLua_F880C30, reinterpret_cast<void*>(&script::ToBoolean)},
        {addr::cSCLua_F880C40, reinterpret_cast<void*>(&script::PushNil)},
        {addr::cSCLua_F880C50, reinterpret_cast<void*>(&script::PushNumber)},
        {addr::cSCLua_F880C60, reinterpret_cast<void*>(&script::PushLString)},
        {addr::cSCLua_F880C70, reinterpret_cast<void*>(&script::PushString)},
        {addr::cSCLua_F880C80, reinterpret_cast<void*>(&script::PushCClosure)},
        {addr::cSCLua_F880C90, reinterpret_cast<void*>(&script::PushUserTag)},
        {addr::cSCLua_F880CA0, reinterpret_cast<void*>(&script::PushBoolean)},
        {addr::cSCLua_F880CB0, reinterpret_cast<void*>(&script::GetGlobal)},
        {addr::cSCLua_F880CC0, reinterpret_cast<void*>(&script::GetTable)},
        {addr::cSCLua_F880CD0, reinterpret_cast<void*>(&script::RawGet)},
        {addr::cSCLua_F880CE0, reinterpret_cast<void*>(&script::RawGetI)},
        {addr::cSCLua_F880CF0, reinterpret_cast<void*>(&script::GetTagMethod)},
        {addr::cSCLua_F880D00, reinterpret_cast<void*>(&script::GetRef)},
        {addr::cSCLua_F880D10, reinterpret_cast<void*>(&script::SetGlobal)},
        {addr::cSCLua_F880D20, reinterpret_cast<void*>(&script::SetTable)},
        {addr::cSCLua_F880D30, reinterpret_cast<void*>(&script::RawSet)},
        {addr::cSCLua_F880D40, reinterpret_cast<void*>(&script::RawSetI)},
        {addr::cSCLua_F880D50, reinterpret_cast<void*>(&script::SetTagMethod)},
        {addr::cSCLua_F880D60, reinterpret_cast<void*>(&script::DoFile)},
        {addr::cSCLua_F880D70, reinterpret_cast<void*>(&script::DoString)},
        {addr::cSCLua_F880D80, reinterpret_cast<void*>(&script::DoBuffer)},
        {addr::cSCLua_F880D90, reinterpret_cast<void*>(&script::GetGCCount)},
        {addr::cSCLua_F880DA0, reinterpret_cast<void*>(&script::GetGCThreshold)},
        {addr::cSCLua_F880DB0, reinterpret_cast<void*>(&script::SetGCThreshold)},
        {addr::cSCLua_F880DC0, reinterpret_cast<void*>(&script::NewTag)},
        {addr::cSCLua_F880DD0, reinterpret_cast<void*>(&script::CopyTagMethods)},
        {addr::cSCLua_F880DE0, reinterpret_cast<void*>(&script::SetTag)},
        {addr::cSCLua_F880DF0, reinterpret_cast<void*>(&script::Unref)},
        {addr::cSCLua_F880E00, reinterpret_cast<void*>(&script::Next)},
        {addr::cSCLua_F880E10, reinterpret_cast<void*>(&script::Error)},
        {addr::cSCLua_F880E20, reinterpret_cast<void*>(&script::Concat)},
        {addr::cSCLua_F880E30, reinterpret_cast<void*>(&script::Register)},
        {addr::cSCLua_F880E40, reinterpret_cast<void*>(&script::CloneTag)},
        {addr::cSCLua_F880E50, reinterpret_cast<void*>(&script::IsFunction)},
        {addr::cSCLua_F880E60, reinterpret_cast<void*>(&script::IsTable)},
        {addr::cSCLua_F880E70, reinterpret_cast<void*>(&script::IsUserData)},
        {addr::cSCLua_F880E80, reinterpret_cast<void*>(&script::IsNil)},
        {addr::cSCLua_F880E90, reinterpret_cast<void*>(&QueryInterfaceOfTarget)},
        {addr::cSCLua_F880EA0, reinterpret_cast<void*>(&AddRefOfTarget)},
        {addr::cSCLua_F880EB0, reinterpret_cast<void*>(&ReleaseOfTarget)},
        {addr::cSCLua_F880EC0, reinterpret_cast<void*>(&script::Open)},
        {addr::cSCLua_F880ED0, reinterpret_cast<void*>(&script::Close)},
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

const bool g_part1Registered = registry::Register({
    "scripts.context.part1",
    "scripts/context",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSCLua_F8809E0,
    "Our script context, part 1: the derived context, its director and its thunks (docs/lua.md)",
    false,
    InstallPart1,
});

} // namespace
} // namespace opensc4::script_context
