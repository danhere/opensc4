// The script context, part 2 (docs/lua.md): the base context's methods
// between 0x008B68D0 and 0x008B8400. The base context is one Lua 5.0 thread
// (gzcom-dll cIGZLua5Thread, vtables 0x00AC8DD0 and 0x00AC8FC0): its thread
// number at +0x4 (-1 = none) and its lua_State at +0x8. Nearly every method
// is one Lua API call wrapped in the precision switch: the game runs the
// x87 at 24 bits, Lua needs 53, so a method switches to 53 bits for the
// call when it finds 24 and puts 24 back afterwards. Replacement, off by
// default:
//
//   scripts.context.part2: the thread's methods (stack, type tests,
//       conversions, pushes, tables, calls, references, globals) and, before
//       them, three functions of the native function table (vtable
//       0x00AC8BC8 at its object's +8): Shutdown, the record vector's
//       resize and AddFunction.
//
// Lua itself (0x008BA5D0 on), the thread table (0x008BCxxx), the record
// vector's helpers and the string code stay the game's and are called by
// address. 0x008B6DE0 is folded with unrelated classes and is not hooked.
//
// A method whose result the original leaves to chance (a "void" one) returns
// what the original leaves in EAX: the Lua call's EAX when the precision was
// not switched, else the precision setter's.
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

namespace field { // the thread
constexpr std::size_t kThreadId = 0x4;
constexpr std::size_t kState = 0x8;
} // namespace field

namespace table { // the native function table, from its interface at +8
constexpr std::size_t kContext = 0x8;
constexpr std::size_t kRecords = 0xC;     // vector of 0x2C-byte records
constexpr std::size_t kRecordsEnd = 0x10;
constexpr std::size_t kFreeList = 0x18;   // list of free record indices
constexpr std::size_t kById = 0x1C;       // hash map id -> record index
constexpr std::size_t kBuckets = 0x20;
constexpr std::size_t kBucketsEnd = 0x24;
constexpr std::size_t kRefA = 0x30;
constexpr std::size_t kRefB = 0x34;
constexpr std::size_t kOwner = 0x38;
constexpr std::size_t kTableName = 0x3C;  // string
constexpr std::size_t kRecordSize = 0x2C; // id, name string at +4, text string at +0x18
} // namespace table

constexpr int32_t kRegistryIndex = -10000;
constexpr int32_t kGlobalsIndex = -10001;

// The x87 precision as found, and whether it was switched to 53 bits.
struct Switch {
    int32_t saved;
    bool on;
};

uint32_t SetPrecision(int32_t precision)
{
    return reinterpret_cast<uint32_t(__cdecl*)(int32_t)>(addr::SC4Precision_Set)(precision);
}

Switch Enter()
{
    Switch sw;
    sw.saved = reinterpret_cast<int32_t(__cdecl*)()>(addr::SC4Precision_Get)();
    sw.on = sw.saved == 0;
    if (sw.on)
        SetPrecision(1);
    return sw;
}

void Leave(const Switch& sw)
{
    if (sw.on)
        SetPrecision(sw.saved);
}

// Leaves, returning what the original has in EAX afterwards.
uint32_t Leave(const Switch& sw, uint32_t eax)
{
    return sw.on ? SetPrecision(sw.saved) : eax;
}

template <typename R, typename... Args>
R Lua(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void* State(char* self)
{
    return At<void*>(self, field::kState);
}

// One Lua call on the thread's state under the switch; the result is kept.
template <typename R, typename... Args>
R Call(char* self, std::uintptr_t function, Args... args)
{
    const Switch sw = Enter();
    const R result = Lua<R>(function, State(self), args...);
    Leave(sw);
    return result;
}

// The same for a call whose result the original does not keep.
template <typename... Args>
uint32_t CallVoid(char* self, std::uintptr_t function, Args... args)
{
    const Switch sw = Enter();
    return Leave(sw, Lua<uint32_t>(function, State(self), args...));
}

// A Lua test (int result) turned into a bool.
template <typename... Args>
uint8_t Test(char* self, std::uintptr_t function, Args... args)
{
    const Switch sw = Enter();
    const uint8_t result = Lua<int32_t>(function, State(self), args...) != 0;
    Leave(sw);
    return result;
}

// The original's cast of (this - 8) to its interface at +8: null only when
// this is 8.
char* Outer(char* self)
{
    return reinterpret_cast<std::uintptr_t>(self) != 8 ? self : nullptr;
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void ReleaseField(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Virtual<uint32_t>(object, 0x8);
    }
}

// ---- The native function table ----

// The game's 0x008B68D0. Tells the owner, drops the references, empties the
// records, the free list and the id map.
uint8_t __fastcall TableShutdown(char* self, void*)
{
    uint8_t ok = 1;
    if (void* owner = At<void*>(self, table::kOwner)) {
        ok = Virtual<uint8_t>(owner, 0x40, Outer(self)) != 0;
        owner = At<void*>(self, table::kOwner);
        if (owner) {
            At<void*>(self, table::kOwner) = nullptr;
            Virtual<uint32_t>(owner, 0x8);
        }
    }
    ReleaseField(self, table::kContext);
    ReleaseField(self, table::kRefA);
    ReleaseField(self, table::kRefB);

    // The vector's clear: both helpers get the address of an unset byte.
    using CopyFn = char*(__cdecl*)(char* first, char* last, char* to, uint8_t* tag, int32_t);
    using DestroyFn = void(__cdecl*)(char* first, char* last, uint8_t* tag);
    uint8_t tag;
    char* const begin = At<char*>(self, table::kRecords);
    char* const end = At<char*>(self, table::kRecordsEnd);
    char* const newEnd = reinterpret_cast<CopyFn>(addr::SCLuaRecords_CopyRange)(end, end, begin, &tag, 0);
    reinterpret_cast<DestroyFn>(addr::SCLuaRecords_DestroyRange)(newEnd, At<char*>(self, table::kRecordsEnd), &tag);
    At<char*>(self, table::kRecordsEnd) = newEnd;

    char* node = At<char*>(At<char*>(self, table::kFreeList), 0);
    while (node != At<char*>(self, table::kFreeList)) {
        char* const dead = node;
        node = At<char*>(node, 0);
        Free(dead);
    }
    char* head = At<char*>(self, table::kFreeList);
    At<char*>(head, 0) = head;
    head = At<char*>(self, table::kFreeList);
    At<char*>(head, 4) = head;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4Uint32HashSet_Clear)(self + table::kById);
    return ok;
}

// The game's 0x008B69A0. `this` is the record vector: resize to `count`
// with an empty record.
void __fastcall RecordsResize(char* records, void*, uint32_t count)
{
    alignas(4) char record[table::kRecordSize];
    char* const made = reinterpret_cast<char*(__thiscall*)(void*)>(addr::SCLuaRecord_Construct)(record);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, char*)>(addr::SCLuaRecords_Resize)(records, count, made);

    void* buffer = At<void*>(record, 0x1C);
    At<std::uintptr_t>(record, 0x18) = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
    buffer = At<void*>(record, 0x8);
    At<std::uintptr_t>(record, 0x4) = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

// The game's 0x008B6A00. (id, name string, text string): gives the function
// a record and stores table[name] = a closure of the dispatcher over (record
// index, this object, the context) in the global table named at +0x3C. For
// an id that has a record already: whether the name is the same.
uint8_t __fastcall TableAddFunction(char* self, void*, uint32_t id, void* name, void* text)
{
    char* const head = At<char*>(self, table::kFreeList);
    if (At<char*>(head, 0) == head) {
        int32_t index = static_cast<int32_t>(
            (At<char*>(self, table::kRecordsEnd) - At<char*>(self, table::kRecords)) /
            static_cast<std::ptrdiff_t>(table::kRecordSize));
        const int32_t limit = index + 0x14;
        RecordsResize(self + table::kRecords, nullptr, static_cast<uint32_t>(limit));
        while (index < limit) {
            char* const list = At<char*>(self, table::kFreeList);
            const int32_t value = index;
            ++index;
            char* const node = reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(0xC);
            At<int32_t>(node, 8) = value; // the original skips this only if node + 8 is null
            char* const last = At<char*>(list, 4);
            At<char*>(node, 0) = list;
            At<char*>(node, 4) = last;
            At<char*>(last, 0) = node;
            At<char*>(list, 4) = node;
        }
    }

    char* const buckets = At<char*>(self, table::kBuckets);
    const uint32_t bucketCount =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(self, table::kBucketsEnd) - buckets) >> 2);
    char* found = At<char*>(buckets, (id % bucketCount) * 4);
    while (found && At<uint32_t>(found, 4) != id)
        found = At<char*>(found, 0);
    if (found) {
        const int32_t index = At<int32_t>(found, 8);
        char* const begin = At<char*>(self, table::kRecords);
        const int32_t count = static_cast<int32_t>(
            (At<char*>(self, table::kRecordsEnd) - begin) / static_cast<std::ptrdiff_t>(table::kRecordSize));
        if (index < count && index >= 0) {
            char* const record = begin + index * static_cast<int32_t>(table::kRecordSize);
            if (At<uint32_t>(record, 0) == id)
                return Virtual<uint8_t>(record + 4, 0x24, name, int32_t{1}); // IsEqual
        }
    }

    // Take the first free record.
    char* const first = At<char*>(At<char*>(self, table::kFreeList), 0);
    const int32_t index = At<int32_t>(first, 8);
    char* const next = At<char*>(first, 0);
    char* const previous = At<char*>(first, 4);
    At<char*>(previous, 0) = next;
    At<char*>(next, 4) = previous;
    Free(first);

    uint32_t key[2] = {id, static_cast<uint32_t>(index)};
    using LookupFn = int32_t*(__thiscall*)(void*, uint32_t* key);
    *reinterpret_cast<LookupFn>(addr::SC4Uint32HashMap_Lookup)(self + table::kById, key) = index;

    char* const record = At<char*>(self, table::kRecords) + index * static_cast<int32_t>(table::kRecordSize);
    At<uint32_t>(record, 0) = id;
    Virtual<uint32_t>(record + 4, 0x34, name);    // Copy
    Virtual<uint32_t>(record + 0x18, 0x34, text);

    const int32_t top = Virtual<int32_t>(At<void*>(self, table::kContext), 0x38); // GetTop
    void* context = At<void*>(self, table::kContext);
    Virtual<uint32_t>(context, 0xCC, Virtual<const char*>(self + table::kTableName, 0x14)); // PushString
    Virtual<uint32_t>(At<void*>(self, table::kContext), 0xE8, kGlobalsIndex);              // RawGet
    uint8_t ok = 0;
    if (Virtual<int32_t>(At<void*>(self, table::kContext), 0x5C, int32_t{-1}) == 5) {      // a table
        context = At<void*>(self, table::kContext);
        Virtual<uint32_t>(context, 0xCC, Virtual<const char*>(record + 4, 0x14));
        Virtual<uint32_t>(At<void*>(self, table::kContext), 0xC4, static_cast<double>(index)); // PushNumber
        Virtual<uint32_t>(At<void*>(self, table::kContext), 0xD8, self - 8);          // PushLightUserData
        context = At<void*>(self, table::kContext);
        Virtual<uint32_t>(context, 0xD8, context);
        Virtual<uint32_t>(At<void*>(self, table::kContext), 0xD0,
                          reinterpret_cast<void*>(addr::SCLuaFunctionTable_Dispatch), int32_t{3}); // PushCClosure
        Virtual<uint32_t>(At<void*>(self, table::kContext), 0x104, int32_t{-3});      // RawSet
        ok = 1;
    }
    Virtual<uint32_t>(At<void*>(self, table::kContext), 0x3C, top); // SetTop
    return ok;
}

// ---- The thread ----

// The game's 0x008B6C30. Pause: lua_yield.
uint32_t __fastcall Pause(char* self, void*, int32_t results)
{
    return CallVoid(self, addr::Lua_Yield, results);
}

// The game's 0x008B6C80. IsValid: the state's owner (the pointer before the
// lua_State) is this thread and it has a number.
uint8_t __fastcall IsValid(char* self, void*)
{
    const Switch sw = Enter();
    uint8_t valid = 0;
    if (char* const state = At<char*>(self, field::kState)) {
        if (*reinterpret_cast<char**>(state - 4) == Outer(self) && At<int32_t>(self, field::kThreadId) != -1)
            valid = 1;
    }
    Leave(sw);
    return valid;
}

// The game's 0x008B6CF0. Destroy: asks the thread table to end this thread.
uint8_t __fastcall Destroy(char* self, void*)
{
    const Switch sw = Enter();
    void* const state = State(self);
    if (!state) {
        Leave(sw);
        return 0;
    }
    uint8_t done = 0;
    if (void* const threads = Lua<void*>(addr::SCLuaThreads_FromState, state)) {
        using EndFn = uint8_t(__thiscall*)(void*, void* state, int32_t id);
        const int32_t id = At<int32_t>(self, field::kThreadId);
        if (reinterpret_cast<EndFn>(addr::SCLuaThreads_EndThread)(threads, State(self), id))
            done = 1;
    }
    Leave(sw);
    return done;
}

// The game's 0x008B6D70. Status: the thread table's status of this thread,
// -1 without a state, a number or a table.
int32_t __fastcall Status(char* self, void*)
{
    const Switch sw = Enter();
    void* const state = State(self);
    if (state && At<int32_t>(self, field::kThreadId) != -1) {
        if (void* const threads = Lua<void*>(addr::SCLuaThreads_FromState, state)) {
            using StatusFn = int32_t(__thiscall*)(void*, int32_t id);
            const int32_t status = reinterpret_cast<StatusFn>(addr::SCLuaThreads_ThreadStatus)(
                threads, At<int32_t>(self, field::kThreadId));
            Leave(sw);
            return status;
        }
    }
    Leave(sw);
    return -1;
}

// The game's 0x008B6DF0. lua_gettop.
int32_t __fastcall GetTop(char* self, void*)
{
    return Call<int32_t>(self, addr::Lua_GetTop);
}

// The game's 0x008B6E30. lua_settop.
uint32_t __fastcall SetTop(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_SetTop, index);
}

// The game's 0x008B6E80. lua_pushvalue.
uint32_t __fastcall PushValue(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_PushValue, index);
}

// The game's 0x008B6ED0. lua_remove.
uint32_t __fastcall Remove(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_Remove, index);
}

// The game's 0x008B6F20. lua_insert.
uint32_t __fastcall Insert(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_Insert, index);
}

// The game's 0x008B6F70. lua_pop: lua_settop(-1 - count).
uint32_t __fastcall Pop(char* self, void*, int32_t count)
{
    return CallVoid(self, addr::Lua_SetTop, static_cast<int32_t>(0xFFFFFFFFu - static_cast<uint32_t>(count)));
}

// The game's 0x008B6FC0. lua_replace.
uint32_t __fastcall Replace(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_Replace, index);
}

// The game's 0x008B7010. Move: lua_xmove(this state, to, count).
uint32_t __fastcall Move(char* self, void*, void* to, int32_t count)
{
    return CallVoid(self, addr::Lua_XMove, to, count);
}

// The game's 0x008B7060. GrowStack: lua_checkstack.
uint8_t __fastcall GrowStack(char* self, void*, int32_t extra)
{
    return Test(self, addr::Lua_CheckStack, extra);
}

// The game's 0x008B70B0. lua_type.
int32_t __fastcall Type(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index);
}

// The game's 0x008B7100. lua_typename.
const char* __fastcall TypeName(char* self, void*, int32_t type)
{
    return Call<const char*>(self, addr::Lua_TypeName, type);
}

// The game's 0x008B7150. lua_isnumber.
uint8_t __fastcall IsNumber(char* self, void*, int32_t index)
{
    return Test(self, addr::Lua_IsNumber, index);
}

// The game's 0x008B71A0. lua_isstring.
uint8_t __fastcall IsString(char* self, void*, int32_t index)
{
    return Test(self, addr::Lua_IsString, index);
}

// The game's 0x008B71F0. lua_iscfunction.
uint8_t __fastcall IsCFunction(char* self, void*, int32_t index)
{
    return Test(self, addr::Lua_IsCFunction, index);
}

// The game's 0x008B7240. lua_type == LUA_TFUNCTION.
uint8_t __fastcall IsFunction(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) == 6;
}

// The game's 0x008B72A0. lua_type == LUA_TTABLE.
uint8_t __fastcall IsTable(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) == 5;
}

// The game's 0x008B7300. lua_isuserdata.
uint8_t __fastcall IsUserData(char* self, void*, int32_t index)
{
    return Test(self, addr::Lua_IsUserData, index);
}

// The game's 0x008B7350. lua_type == LUA_TLIGHTUSERDATA.
uint8_t __fastcall IsLightUserData(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) == 2;
}

// The game's 0x008B73B0. lua_type == LUA_TBOOLEAN.
uint8_t __fastcall IsBoolean(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) == 1;
}

// The game's 0x008B7410. lua_type == LUA_TNIL.
uint8_t __fastcall IsNil(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) == 0;
}

// The game's 0x008B7470. lua_type == LUA_TNONE.
uint8_t __fastcall IsNone(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) == -1;
}

// The game's 0x008B74D0. lua_type <= 0.
uint8_t __fastcall IsNoneOrNil(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Type, index) <= 0;
}

// The game's 0x008B7520. lua_equal.
uint8_t __fastcall Equal(char* self, void*, int32_t a, int32_t b)
{
    return Test(self, addr::Lua_Equal, a, b);
}

// The game's 0x008B7580. lua_rawequal.
uint8_t __fastcall RawEqual(char* self, void*, int32_t a, int32_t b)
{
    return Test(self, addr::Lua_RawEqual, a, b);
}

// The game's 0x008B75E0. lua_lessthan.
uint8_t __fastcall LessThan(char* self, void*, int32_t a, int32_t b)
{
    return Test(self, addr::Lua_LessThan, a, b);
}

// The game's 0x008B7640 (two slots, ToUint32 and ToSint32): lua_tonumber
// truncated, inside the switch.
int32_t __fastcall ToInteger(char* self, void*, int32_t index)
{
    const Switch sw = Enter();
    const double number = Lua<double>(addr::Lua_ToNumber, State(self), index);
    const int32_t result = static_cast<int32_t>(static_cast<int64_t>(number));
    Leave(sw);
    return result;
}

// The game's 0x008B7690. lua_tonumber.
double __fastcall ToNumber(char* self, void*, int32_t index)
{
    return Call<double>(self, addr::Lua_ToNumber, index);
}

// The game's 0x008B76F0. lua_toboolean.
uint8_t __fastcall ToBoolean(char* self, void*, int32_t index)
{
    return Test(self, addr::Lua_ToBoolean, index);
}

// The game's 0x008B7740. lua_tostring.
const char* __fastcall ToString(char* self, void*, int32_t index)
{
    return Call<const char*>(self, addr::Lua_ToString, index);
}

// The game's 0x008B7790. lua_strlen.
uint32_t __fastcall Strlen(char* self, void*, int32_t index)
{
    return Call<uint32_t>(self, addr::Lua_StrLen, index);
}

// The game's 0x008B77E0. lua_tocfunction.
void* __fastcall ToCFunction(char* self, void*, int32_t index)
{
    return Call<void*>(self, addr::Lua_ToCFunction, index);
}

// The game's 0x008B7830. lua_touserdata.
void* __fastcall ToUserData(char* self, void*, int32_t index)
{
    return Call<void*>(self, addr::Lua_ToUserData, index);
}

// The game's 0x008B7880. lua_tothread.
void* __fastcall ToThread(char* self, void*, int32_t index)
{
    return Call<void*>(self, addr::Lua_ToThread, index);
}

// The game's 0x008B78D0. lua_pushnil.
uint32_t __fastcall PushNil(char* self, void*)
{
    return CallVoid(self, addr::Lua_PushNil);
}

// The game's 0x008B7910. lua_pushnumber.
uint32_t __fastcall PushNumber(char* self, void*, double number)
{
    return CallVoid(self, addr::Lua_PushNumber, number);
}

// The game's 0x008B7960. lua_pushlstring.
uint32_t __fastcall PushLString(char* self, void*, const char* text, uint32_t length)
{
    return CallVoid(self, addr::Lua_PushLString, text, length);
}

// The game's 0x008B79B0. lua_pushstring.
uint32_t __fastcall PushString(char* self, void*, const char* text)
{
    return CallVoid(self, addr::Lua_PushString, text);
}

// The game's 0x008B7A00. lua_pushcclosure.
uint32_t __fastcall PushCClosure(char* self, void*, void* function, int32_t upvalues)
{
    return CallVoid(self, addr::Lua_PushCClosure, function, upvalues);
}

// The game's 0x008B7A50. lua_pushboolean of the argument's low byte.
uint32_t __fastcall PushBoolean(char* self, void*, uint32_t value)
{
    return CallVoid(self, addr::Lua_PushBoolean, int32_t{(value & 0xFF) != 0});
}

// The game's 0x008B7AA0. lua_pushlightuserdata.
uint32_t __fastcall PushLightUserData(char* self, void*, void* pointer)
{
    return CallVoid(self, addr::Lua_PushLightUserData, pointer);
}

// The game's 0x008B7AF0. lua_newuserdata.
void* __fastcall NewUserData(char* self, void*, uint32_t size)
{
    return Call<void*>(self, addr::Lua_NewUserData, size);
}

// The game's 0x008B7B40. lua_gettable.
uint32_t __fastcall GetTable(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_GetTable, index);
}

// The game's 0x008B7B90. lua_rawget.
uint32_t __fastcall RawGet(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_RawGet, index);
}

// The game's 0x008B7BE0. lua_rawgeti.
uint32_t __fastcall RawGetI(char* self, void*, int32_t index, int32_t n)
{
    return CallVoid(self, addr::Lua_RawGetI, index, n);
}

// The game's 0x008B7C30. lua_newtable.
uint32_t __fastcall NewTable(char* self, void*)
{
    return CallVoid(self, addr::Lua_NewTable);
}

// The game's 0x008B7C70. lua_getfenv.
uint32_t __fastcall GetFunctionEnvironment(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_GetFEnv, index);
}

// The game's 0x008B7CC0. lua_getmetatable.
int32_t __fastcall GetMetaTable(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_GetMetaTable, index);
}

// The game's 0x008B7D10. Pushes the registry: lua_pushvalue(LUA_REGISTRYINDEX).
uint32_t __fastcall GetRegistryTable(char* self, void*)
{
    return CallVoid(self, addr::Lua_PushValue, kRegistryIndex);
}

// The game's 0x008B7D60. Pushes the globals: lua_pushvalue(LUA_GLOBALSINDEX).
uint32_t __fastcall GetGlobalTable(char* self, void*)
{
    return CallVoid(self, addr::Lua_PushValue, kGlobalsIndex);
}

// The game's 0x008B7DB0. lua_settable.
uint32_t __fastcall SetTable(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_SetTable, index);
}

// The game's 0x008B7E00. lua_rawset.
uint32_t __fastcall RawSet(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_RawSet, index);
}

// The game's 0x008B7E50. lua_rawseti.
uint32_t __fastcall RawSetI(char* self, void*, int32_t index, int32_t n)
{
    return CallVoid(self, addr::Lua_RawSetI, index, n);
}

// The game's 0x008B7EA0. lua_setfenv; its result is not kept.
uint32_t __fastcall SetFunctionEnvironment(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_SetFEnv, index);
}

// The game's 0x008B7EF0. lua_setmetatable; its result is not kept.
uint32_t __fastcall SetMetaTable(char* self, void*, int32_t index)
{
    return CallVoid(self, addr::Lua_SetMetaTable, index);
}

// The game's 0x008B7F40. lua_call.
uint32_t __fastcall RawCall(char* self, void*, int32_t arguments, int32_t results)
{
    return CallVoid(self, addr::Lua_Call, arguments, results);
}

// The game's 0x008B7F90. lua_dofile.
int32_t __fastcall DoFile(char* self, void*, const char* name)
{
    return Call<int32_t>(self, addr::Lua_DoFile, name);
}

// The game's 0x008B7FE0. lua_dostring.
int32_t __fastcall DoString(char* self, void*, const char* text)
{
    return Call<int32_t>(self, addr::Lua_DoString, text);
}

// The game's 0x008B8030. lua_dobuffer.
int32_t __fastcall DoBuffer(char* self, void*, const char* buffer, uint32_t size, const char* name)
{
    return Call<int32_t>(self, addr::Lua_DoBuffer, buffer, size, name);
}

// The game's 0x008B8090. lua_load.
int32_t __fastcall Load(char* self, void*, void* reader, void* data, const char* name)
{
    return Call<int32_t>(self, addr::Lua_Load, reader, data, name);
}

// The game's 0x008B80F0. lua_error; the argument is not used. When Lua
// unwinds, the precision stays switched, as in the original.
uint32_t __fastcall Error(char* self, void*, const char*)
{
    return CallVoid(self, addr::Lua_Error);
}

// The game's 0x008B8130. lua_next.
int32_t __fastcall Next(char* self, void*, int32_t index)
{
    return Call<int32_t>(self, addr::Lua_Next, index);
}

// The game's 0x008B8180. lua_concat.
uint32_t __fastcall Concat(char* self, void*, int32_t count)
{
    return CallVoid(self, addr::Lua_Concat, count);
}

// The game's 0x008B81D0. Register: globals[name] = function.
uint32_t __fastcall Register(char* self, void*, void* function, const char* name)
{
    const Switch sw = Enter();
    Lua<uint32_t>(addr::Lua_PushString, State(self), name);
    Lua<uint32_t>(addr::Lua_PushCClosure, State(self), function, int32_t{0});
    return Leave(sw, Lua<uint32_t>(addr::Lua_SetTable, State(self), kGlobalsIndex));
}

// The game's 0x008B8240. lua_upvalueindex; `this` is not used.
int32_t __fastcall GetUpValueIndex(char*, void*, int32_t index)
{
    const Switch sw = Enter();
    const int32_t result = static_cast<int32_t>(0xFFFFD8EFu - static_cast<uint32_t>(index));
    Leave(sw);
    return result;
}

// The game's 0x008B8280. GetGlobal: pushes globals[name].
uint32_t __fastcall GetGlobal(char* self, void*, const char* name)
{
    const Switch sw = Enter();
    Lua<uint32_t>(addr::Lua_PushString, State(self), name);
    return Leave(sw, Lua<uint32_t>(addr::Lua_GetTable, State(self), kGlobalsIndex));
}

// The game's 0x008B82E0. SetGlobal: globals[name] = the value on top.
uint32_t __fastcall SetGlobal(char* self, void*, const char* name)
{
    const Switch sw = Enter();
    Lua<uint32_t>(addr::Lua_PushString, State(self), name);
    Lua<uint32_t>(addr::Lua_Insert, State(self), int32_t{-2});
    return Leave(sw, Lua<uint32_t>(addr::Lua_SetTable, State(self), kGlobalsIndex));
}

// The game's 0x008B8340. Ref: luaL_ref in the registry.
int32_t __fastcall Ref(char* self, void*)
{
    return Call<int32_t>(self, addr::LuaL_Ref, kRegistryIndex);
}

// The game's 0x008B8390. GetRef: lua_rawgeti(registry, reference).
uint32_t __fastcall GetRef(char* self, void*, int32_t reference)
{
    return CallVoid(self, addr::Lua_RawGetI, kRegistryIndex, reference);
}

// The game's 0x008B83E0. Unref: luaL_unref in the registry, without the
// precision switch.
uint32_t __fastcall Unref(char* self, void*, int32_t reference)
{
    return Lua<uint32_t>(addr::LuaL_Unref, State(self), kRegistryIndex, reference);
}

bool InstallPart2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[80] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSCLua_F8B68D0, reinterpret_cast<void*>(&TableShutdown)},
        {addr::cSCLua_F8B69A0, reinterpret_cast<void*>(&RecordsResize)},
        {addr::cSCLua_F8B6A00, reinterpret_cast<void*>(&TableAddFunction)},
        {addr::cSCLua_F8B6C30, reinterpret_cast<void*>(&Pause)},
        {addr::cSCLua_F8B6C80, reinterpret_cast<void*>(&IsValid)},
        {addr::cSCLua_F8B6CF0, reinterpret_cast<void*>(&Destroy)},
        {addr::cSCLua_F8B6D70, reinterpret_cast<void*>(&Status)},
        {addr::cSCLua_F8B6DF0, reinterpret_cast<void*>(&GetTop)},
        {addr::cSCLua_F8B6E30, reinterpret_cast<void*>(&SetTop)},
        {addr::cSCLua_F8B6E80, reinterpret_cast<void*>(&PushValue)},
        {addr::cSCLua_F8B6ED0, reinterpret_cast<void*>(&Remove)},
        {addr::cSCLua_F8B6F20, reinterpret_cast<void*>(&Insert)},
        {addr::cSCLua_F8B6F70, reinterpret_cast<void*>(&Pop)},
        {addr::cSCLua_F8B6FC0, reinterpret_cast<void*>(&Replace)},
        {addr::cSCLua_F8B7010, reinterpret_cast<void*>(&Move)},
        {addr::cSCLua_F8B7060, reinterpret_cast<void*>(&GrowStack)},
        {addr::cSCLua_F8B70B0, reinterpret_cast<void*>(&Type)},
        {addr::cSCLua_F8B7100, reinterpret_cast<void*>(&TypeName)},
        {addr::cSCLua_F8B7150, reinterpret_cast<void*>(&IsNumber)},
        {addr::cSCLua_F8B71A0, reinterpret_cast<void*>(&IsString)},
        {addr::cSCLua_F8B71F0, reinterpret_cast<void*>(&IsCFunction)},
        {addr::cSCLua_F8B7240, reinterpret_cast<void*>(&IsFunction)},
        {addr::cSCLua_F8B72A0, reinterpret_cast<void*>(&IsTable)},
        {addr::cSCLua_F8B7300, reinterpret_cast<void*>(&IsUserData)},
        {addr::cSCLua_F8B7350, reinterpret_cast<void*>(&IsLightUserData)},
        {addr::cSCLua_F8B73B0, reinterpret_cast<void*>(&IsBoolean)},
        {addr::cSCLua_F8B7410, reinterpret_cast<void*>(&IsNil)},
        {addr::cSCLua_F8B7470, reinterpret_cast<void*>(&IsNone)},
        {addr::cSCLua_F8B74D0, reinterpret_cast<void*>(&IsNoneOrNil)},
        {addr::cSCLua_F8B7520, reinterpret_cast<void*>(&Equal)},
        {addr::cSCLua_F8B7580, reinterpret_cast<void*>(&RawEqual)},
        {addr::cSCLua_F8B75E0, reinterpret_cast<void*>(&LessThan)},
        {addr::cSCLua_F8B7640, reinterpret_cast<void*>(&ToInteger)},
        {addr::cSCLua_F8B7690, reinterpret_cast<void*>(&ToNumber)},
        {addr::cSCLua_F8B76F0, reinterpret_cast<void*>(&ToBoolean)},
        {addr::cSCLua_F8B7740, reinterpret_cast<void*>(&ToString)},
        {addr::cSCLua_F8B7790, reinterpret_cast<void*>(&Strlen)},
        {addr::cSCLua_F8B77E0, reinterpret_cast<void*>(&ToCFunction)},
        {addr::cSCLua_F8B7830, reinterpret_cast<void*>(&ToUserData)},
        {addr::cSCLua_F8B7880, reinterpret_cast<void*>(&ToThread)},
        {addr::cSCLua_F8B78D0, reinterpret_cast<void*>(&PushNil)},
        {addr::cSCLua_F8B7910, reinterpret_cast<void*>(&PushNumber)},
        {addr::cSCLua_F8B7960, reinterpret_cast<void*>(&PushLString)},
        {addr::cSCLua_F8B79B0, reinterpret_cast<void*>(&PushString)},
        {addr::cSCLua_F8B7A00, reinterpret_cast<void*>(&PushCClosure)},
        {addr::cSCLua_F8B7A50, reinterpret_cast<void*>(&PushBoolean)},
        {addr::cSCLua_F8B7AA0, reinterpret_cast<void*>(&PushLightUserData)},
        {addr::cSCLua_F8B7AF0, reinterpret_cast<void*>(&NewUserData)},
        {addr::cSCLua_F8B7B40, reinterpret_cast<void*>(&GetTable)},
        {addr::cSCLua_F8B7B90, reinterpret_cast<void*>(&RawGet)},
        {addr::cSCLua_F8B7BE0, reinterpret_cast<void*>(&RawGetI)},
        {addr::cSCLua_F8B7C30, reinterpret_cast<void*>(&NewTable)},
        {addr::cSCLua_F8B7C70, reinterpret_cast<void*>(&GetFunctionEnvironment)},
        {addr::cSCLua_F8B7CC0, reinterpret_cast<void*>(&GetMetaTable)},
        {addr::cSCLua_F8B7D10, reinterpret_cast<void*>(&GetRegistryTable)},
        {addr::cSCLua_F8B7D60, reinterpret_cast<void*>(&GetGlobalTable)},
        {addr::cSCLua_F8B7DB0, reinterpret_cast<void*>(&SetTable)},
        {addr::cSCLua_F8B7E00, reinterpret_cast<void*>(&RawSet)},
        {addr::cSCLua_F8B7E50, reinterpret_cast<void*>(&RawSetI)},
        {addr::cSCLua_F8B7EA0, reinterpret_cast<void*>(&SetFunctionEnvironment)},
        {addr::cSCLua_F8B7EF0, reinterpret_cast<void*>(&SetMetaTable)},
        {addr::cSCLua_F8B7F40, reinterpret_cast<void*>(&RawCall)},
        {addr::cSCLua_F8B7F90, reinterpret_cast<void*>(&DoFile)},
        {addr::cSCLua_F8B7FE0, reinterpret_cast<void*>(&DoString)},
        {addr::cSCLua_F8B8030, reinterpret_cast<void*>(&DoBuffer)},
        {addr::cSCLua_F8B8090, reinterpret_cast<void*>(&Load)},
        {addr::cSCLua_F8B80F0, reinterpret_cast<void*>(&Error)},
        {addr::cSCLua_F8B8130, reinterpret_cast<void*>(&Next)},
        {addr::cSCLua_F8B8180, reinterpret_cast<void*>(&Concat)},
        {addr::cSCLua_F8B81D0, reinterpret_cast<void*>(&Register)},
        {addr::cSCLua_F8B8240, reinterpret_cast<void*>(&GetUpValueIndex)},
        {addr::cSCLua_F8B8280, reinterpret_cast<void*>(&GetGlobal)},
        {addr::cSCLua_F8B82E0, reinterpret_cast<void*>(&SetGlobal)},
        {addr::cSCLua_F8B8340, reinterpret_cast<void*>(&Ref)},
        {addr::cSCLua_F8B8390, reinterpret_cast<void*>(&GetRef)},
        {addr::cSCLua_F8B83E0, reinterpret_cast<void*>(&Unref)},
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

const bool g_part2Registered = registry::Register({
    "scripts.context.part2",
    "scripts/context",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSCLua_F8B6DF0,
    "Our script thread methods (one Lua call each in the precision switch) and the native function table",
    false,
    InstallPart2,
});

} // namespace
} // namespace opensc4::script_context
