// Our automata script system, part s3a (build 641): the loading of the
// automata scripts. LoadScripts (Init's 0x00523B10) makes the loader's Lua
// state (0x0051C4C0, the game's: it holds the Lua print patch site), runs
// the script group 0x4A5E8F3F and reads what it defined with the readers:
// the attractor and generator templates (0x00523500, 0x00522F30) with
// their parts (the flags 0x0051C5D0, the calendar 0x0051CB40, the sounds
// 0x0051CBF0, the ignored states 0x0051CD20, the time of day curve
// 0x0051F010 with its point insert 0x0051EE10, the triggers 0x0051F2A0, the
// behaviours 0x00522860), the templates' automata groups by name
// (0x0051DAA0), the groups' parents and member names (0x0051F5C0), the
// drive commands (0x0051FA60) and the track buildings (0x0051EEE0); the
// groups and the vehicle kinds (0x00521C70, 0x005216A0, part s3b) are the
// game's, called at their addresses. Then the Lua state is shut down and
// released. The layout is in src/core/automata_scripts.h and the survey in
// docs/automata.md, "Part s". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Groups, off by default:
//   simulators.automata.scripts.loader (5): kLoader below.
//   simulators.automata.scripts.readers (10): kReaders.
//
// The Lua state (the script system's +0x18) is the game's script context
// interface; the slots used here: 0x10 shut down, 0x18 the top, 0x2C pop n,
// 0x38 a number?, 0x3C a string?, 0x54 to uint32, 0x5C to number (ST0),
// 0x60 to string, 0x70 to userdata, 0x78 push nil, 0x84 push string, 0xAC
// push a reference, 0xBC set table, 0xF8 drop a reference, 0x120 a table?,
// 0x128 nil?. The field readers (SC4Script::GetField, SC4ScriptField::*,
// SC4ScriptValue::*, the table walk) are the game's, called at their
// addresses; a table walk is 0x20 bytes: the state, an SC4String at +4,
// the table's absolute index at +0x18, the end flag at +0x1C. Each
// template table's fields are read at that index of a walk made over the
// entry's value only for it.
//
// Oddities kept (all as the game does them):
// - The readers empty a vector by "erasing" [end, end), which leaves the end
//   at the begin (the memmove of the empty range is never made).
// - A template of the wrong class (the other reader's) gets the refusal
//   message formatted and dropped and is then read as if it were right.
// - After one entry's reader fails, the later entries are still made and
//   named and their max_count read (the attractor's radius and strength
//   curve, the generator's radius, count kind and the fields after it,
//   too), but the parts in between are skipped; the answer is false.
// - The attractor's strength table of one value (or none) makes two curve
//   points that read its first value even when there is none; the step
//   of the curve is the radius over the count less one, the count read as
//   a signed int and corrected by 2^32 (it never is negative).
// - The generator's priority byte is the value capped at 255, 25 when it
//   does not read.
// - LoadScripts answers true when the script group did not run (its error
//   message is formatted and dropped).
// - The drive command reader, a property list of 13 entries of up to 8
//   values, maps each value to its entry's index.
//
// Floats: the strength curve's step is a float (the division in double,
// stored to a float), the running x a double in the x87 register, stored to
// a float per point; the time of day curve's points are the Lua numbers as
// floats, inserted before the first point of a larger x (unordered is not
// larger: a NaN goes last). Lua numbers the game stores as floats are
// rounded from the double ST0 at the store.
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

// A game string copied from a C string as the readers inline it (the
// length plus one allocated, STL's length error at npos).
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

// A uint32 vector (begin, end, capacity) as the game keeps them.
struct U32Vector {
    std::uint32_t* begin;
    std::uint32_t* end;
    std::uint32_t* capacity;
};

void PushBack(U32Vector& v, std::uint32_t value)
{
    if (v.end == v.capacity) {
        std::uint8_t tag = 0;
        CallThis<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(&v), v.end,
                       static_cast<const std::uint32_t*>(&value), static_cast<void*>(&tag), 1u, 1u);
        return;
    }
    if (v.end)
        *v.end = value;
    ++v.end;
}

// The readers' empty of a vector: erase [end, end) leaves the end at the
// begin.
void EmptyVector(void* vector) { At<void*>(vector, 4) = At<void*>(vector, 0); }

// A list head's nodes destroyed as the readers inline it: each node's
// SC4String (+8) destroyed (its slot 0x70 with 0), the node freed, the head
// left empty.
void ClearStringList(char* head)
{
    char* node = At<char*>(head, 0);
    while (node != head) {
        char* const next = At<char*>(node, 0);
        Virtual<std::uint32_t>(node + 8, 0x70, 0u);
        Free(node);
        node = next; // the game re-reads the head each time; it does not change
    }
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
}

// A list of uint32 nodes (0xC bytes, the value at +8) appended to.
void ListAppend(char* head, std::uint32_t value)
{
    char* const node = static_cast<char*>(Allocate(0xC));
    if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0)
        At<std::uint32_t>(node, 8) = value;
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

// The script state and the field readers.
void* Context(char* self) { return At<void*>(self, field::kObject18); }

bool GetField(void* context, std::uintptr_t name, std::int32_t index)
{
    return CallCdecl<std::uint8_t>(addr::SC4Script_GetField, context, Text(name), index) != 0;
}

bool FieldFloat(void* context, std::uintptr_t name, void* out, std::int32_t index)
{
    return CallCdecl<std::uint8_t>(addr::SC4ScriptField_Float, context, Text(name), out, index) != 0;
}

bool FieldUint32(void* context, std::uintptr_t name, void* out, std::int32_t index)
{
    return CallCdecl<std::uint8_t>(addr::SC4ScriptField_Uint32, context, Text(name), out, index) != 0;
}

bool FieldBool(void* context, std::uintptr_t name, void* out, std::int32_t index)
{
    return CallCdecl<std::uint8_t>(addr::SC4ScriptField_Bool, context, Text(name), out, index) != 0;
}

void Pop(void* context) { Virtual<std::uint32_t>(context, 0x2C, 1); }

// A table walk (0x20 bytes) and its string's destruction, inline in the
// game.
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

void WalkGlobal(TableWalk& walk, void* context, const char* name)
{
    CallThis<void*>(addr::SC4ScriptTableWalk_ConstructGlobal, walk.bytes, context, name);
}

// The hash map iterator of SC4HashTable::Begin and its increment over a
// string map (h * 5 + the signed char, modulo the bucket count).
struct Iterator {
    char* node;
    char* table;
};

std::uint32_t BucketCount(char* table)
{
    const std::int32_t bytes = static_cast<std::int32_t>(At<char*>(table, 8) - At<char*>(table, 4));
    return static_cast<std::uint32_t>(bytes >> 2);
}

char* NextStringNode(char* node, char* table)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    const char* const begin = At<const char*>(node, 4);
    const std::uint32_t length = static_cast<std::uint32_t>(At<const char*>(node, 8) - begin);
    std::uint32_t h = 0;
    for (std::uint32_t i = 0; i < length; ++i)
        h = h * 5 + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(begin[i])));
    for (std::uint32_t i = h % BucketCount(table) + 1; i < BucketCount(table); ++i)
        if (char* const found = At<char**>(table, 4)[i])
            return found;
    return nullptr;
}

char* FirstNode(char* table)
{
    for (std::uint32_t i = 0; i < BucketCount(table); ++i)
        if (char* const node = At<char**>(table, 4)[i])
            return node;
    return nullptr;
}

constexpr std::uint32_t kAttractorClass = 0x6A1628BAu, kGeneratorClass = 0x4A1628C5u;

// ====================================================================================
// The template parts

// 0x0051C5D0 (index, template), ret 8: +0x58 = 2, then nine flags from
// boolean fields.
std::uint8_t __fastcall ReadFlags(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadFlags", logged);
    void* const context = Context(self);
    At<std::uint32_t>(t, 0x58) = 2;
    struct Flag {
        std::uintptr_t name;
        std::uint32_t bit;
    };
    static constexpr Flag kFlags[] = {
        {addr::cSC4AutomataScriptSystem_Key_use_lot_facing, 0x10},
        {addr::cSC4AutomataScriptSystem_Key_use_lot_center, 0x20},
        {addr::cSC4AutomataScriptSystem_Key_ignore_lot, 0x40},
        {addr::cSC4AutomataScriptSystem_Key_transient, 0x8},
        {addr::cSC4AutomataScriptSystem_Key_ground_only, 0x1000},
        {addr::cSC4AutomataScriptSystem_Key_destroy_automata, 0x100},
        {addr::cSC4AutomataScriptSystem_Key_follow_roads, 0x80},
        {addr::cSC4AutomataScriptSystem_Key_follow_rail, 0x800},
        {addr::cSC4AutomataScriptSystem_Key_persist_offscreen, 0x200},
    };
    for (const Flag& f : kFlags) {
        std::uint8_t on = 0;
        FieldBool(context, f.name, &on, index);
        if (on)
            At<std::uint32_t>(t, 0x58) |= f.bit;
        else
            At<std::uint32_t>(t, 0x58) &= ~f.bit;
    }
    return 1;
}

// 0x0051CB40 (index, template), ret 8: `calendar`, a list of bit numbers,
// as the mask +0x34 (-1 without the field).
std::uint8_t __fastcall ReadCalendar(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadCalendar", logged);
    void* const context = Context(self);
    if (!GetField(context, addr::cSC4AutomataScriptSystem_Key_calendar, index)) {
        At<std::uint32_t>(t, 0x34) = 0xFFFFFFFFu;
        return 1;
    }
    At<std::uint32_t>(t, 0x34) = 0;
    U32Vector bits{nullptr, nullptr, nullptr};
    CallCdecl<void>(addr::SC4ScriptValue_Uint32Vector, context, static_cast<void*>(&bits), -1);
    for (std::uint32_t* b = bits.begin; b != bits.end; ++b)
        At<std::uint32_t>(t, 0x34) |= 1u << (*b & 31);
    Pop(context);
    if (bits.begin)
        Free(bits.begin);
    return 1;
}

// 0x0051CBF0 (index, template), ret 8: `sfx`, a list of hex strings, as the
// IDs of +0x60.
std::uint8_t __fastcall ReadSounds(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadSounds", logged);
    void* const context = Context(self);
    if (!GetField(context, addr::cSC4AutomataScriptSystem_Key_sfx, index))
        return 1;
    struct {
        char* begin;
        char* end;
        char* capacity;
    } strings{nullptr, nullptr, nullptr};
    CallCdecl<void>(addr::SC4ScriptValue_StringVector, context, static_cast<void*>(&strings), -1);
    U32Vector& ids = At<U32Vector>(t, 0x60);
    EmptyVector(&ids);
    for (char* s = strings.begin; s != strings.end; s += 0x14) {
        const char* const text = Virtual<const char*>(s, 0x14);
        const std::uint32_t id = CallCdecl<std::uint32_t>(addr::SC4_strtoul, text, static_cast<char**>(nullptr), 16);
        PushBack(ids, id);
    }
    Pop(context);
    for (char* s = strings.begin; s != strings.end; s += 0x14)
        Virtual<std::uint32_t>(s, 0x70, 0u);
    if (strings.begin)
        Free(strings.begin);
    return 1;
}

// 0x0051CD20 (index, template), ret 8: `ignore_states` into +0x6C.
std::uint8_t __fastcall ReadIgnoredStates(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadIgnoredStates", logged);
    void* const context = Context(self);
    EmptyVector(t + 0x6C);
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_ignore_states, index)) {
        CallCdecl<void>(addr::SC4ScriptValue_Uint32Vector, context, static_cast<void*>(t + 0x6C), -1);
        Pop(context);
    }
    return 1;
}

struct PointVector {
    float* begin;
    float* end;
    float* capacity;
};

// 0x0051EE10 (this = a point vector; position, point), ret 8: the insert of
// one (x, y) pair; answers the inserted point's place.
float* __fastcall InsertPoint(PointVector* self, void*, float* position, const float* point)
{
    static bool logged;
    FirstCall("InsertPoint", logged);
    const std::int32_t at = static_cast<std::int32_t>(reinterpret_cast<char*>(position) -
                                                      reinterpret_cast<char*>(self->begin)) >>
                            3;
    if (self->end == self->capacity) {
        std::uint32_t tag = 0;
        CallThis<void>(addr::SC4PointVector_InsertN, static_cast<void*>(self), position, point,
                       static_cast<void*>(&tag), 1u, 0u);
        return self->begin + 2 * at;
    }
    if (position == self->end) {
        if (self->end) {
            At<std::uint32_t>(self->end, 0) = At<std::uint32_t>(const_cast<float*>(point), 0);
            At<std::uint32_t>(self->end, 4) = At<std::uint32_t>(const_cast<float*>(point), 4);
        }
        self->end += 2;
        return self->begin + 2 * at;
    }
    if (self->end) {
        At<std::uint32_t>(self->end, 0) = At<std::uint32_t>(self->end, -8);
        At<std::uint32_t>(self->end, 4) = At<std::uint32_t>(self->end, -4);
    }
    self->end += 2;
    const std::uint32_t x = At<std::uint32_t>(const_cast<float*>(point), 0);
    const std::uint32_t y = At<std::uint32_t>(const_cast<float*>(point), 4);
    std::uint32_t tag = 0;
    CallCdecl<void*>(addr::SC4Vector2Vector_CopyBackward, position, self->end - 4, self->end - 2,
                     static_cast<void*>(&tag));
    At<std::uint32_t>(position, 0) = x;
    At<std::uint32_t>(position, 4) = y;
    return self->begin + 2 * at;
}

// The curve point append the time of day reader inlines when the vector is
// full: a new buffer of the count plus the larger of it and 1.
void AppendPointGrow(PointVector& points, const float* point)
{
    float* const old = points.begin;
    float* const end = points.end;
    const std::uint32_t count = static_cast<std::uint32_t>((end - old) / 2);
    const std::uint32_t grown = count + (count < 1 ? 1u : count);
    float* const buffer = grown ? static_cast<float*>(Allocate(grown * 8)) : nullptr;
    float* out = buffer;
    for (float* p = old; p != end; p += 2, out += 2)
        if (out) {
            At<std::uint32_t>(out, 0) = At<std::uint32_t>(p, 0);
            At<std::uint32_t>(out, 4) = At<std::uint32_t>(p, 4);
        }
    if (out) {
        At<std::uint32_t>(out, 0) = At<std::uint32_t>(const_cast<float*>(point), 0);
        At<std::uint32_t>(out, 4) = At<std::uint32_t>(const_cast<float*>(point), 4);
    }
    if (old)
        Free(old);
    points.begin = buffer;
    points.end = out + 2;
    points.capacity = buffer + 2 * grown;
}

// The default curve points both curve readers append: (x, y) through
// SC4PointVector::InsertN when full.
void AppendPoint(PointVector& points, std::uint32_t x, std::uint32_t y, void* tag)
{
    std::uint32_t point[2] = {x, y};
    if (points.end == points.capacity) {
        CallThis<void>(addr::SC4PointVector_InsertN, static_cast<void*>(&points), points.end,
                       reinterpret_cast<const float*>(point), tag, 1u, 1u);
        return;
    }
    if (points.end) {
        At<std::uint32_t>(points.end, 0) = x;
        At<std::uint32_t>(points.end, 4) = y;
    }
    points.end += 2;
}

// 0x0051F010 (index, template), ret 8: `time_of_day`, a table x -> y, as
// the curve +0x24, the points in x order (each before the first larger x);
// without the field the points (0, 1) and (24, 1).
std::uint8_t __fastcall ReadTimeOfDay(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadTimeOfDay", logged);
    void* const context = Context(self);
    PointVector points{nullptr, nullptr, nullptr};
    std::uint32_t tag = 0;
    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_time_of_day, index)) {
        TableWalk walk;
        WalkAt(walk, context, -1);
        CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
        while (!walk.Done()) {
            float point[2];
            point[0] = static_cast<float>(Virtual<long double>(context, 0x5C, -2));
            point[1] = static_cast<float>(Virtual<long double>(context, 0x5C, -1));
            float* at = points.begin;
            bool placed = false;
            for (; at != points.end; at += 2)
                if (point[0] < *at) {
                    CallThis<float*>(addr::cSC4AutomataScriptSystem_InsertPoint, static_cast<void*>(&points), at,
                                     static_cast<const float*>(point));
                    placed = at != points.end;
                    break;
                }
            if (!placed) {
                if (points.end != points.capacity) {
                    if (points.end) {
                        At<std::uint32_t>(points.end, 0) = At<std::uint32_t>(point, 0);
                        At<std::uint32_t>(points.end, 4) = At<std::uint32_t>(point, 4);
                    }
                    points.end += 2;
                } else {
                    AppendPointGrow(points, point);
                }
            }
            CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
        }
        Pop(context);
        walk.DestroyString();
    } else {
        AppendPoint(points, 0u, 0x3F800000u, &tag);
        AppendPoint(points, 0x41C00000u, 0x3F800000u, &tag);
    }
    CallThis<std::uint8_t>(addr::SC4Curve_SetPoints, t + 0x24, static_cast<const void*>(&points));
    if (points.begin)
        Free(points.begin);
    return 1;
}

// 0x0051F2A0 (index, template), ret 8: `effect_trigger` and
// `deactivate_trigger`, lists of hex strings, as the ID lists +0x44 and
// +0x48 (each emptied first).
std::uint8_t __fastcall ReadTriggers(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadTriggers", logged);
    void* const context = Context(self);
    struct List {
        std::size_t at;
        std::uintptr_t name;
    };
    static constexpr List kLists[] = {
        {0x44, addr::cSC4AutomataScriptSystem_Key_effect_trigger},
        {0x48, addr::cSC4AutomataScriptSystem_Key_deactivate_trigger},
    };
    for (const List& l : kLists) {
        ClearListNodes(At<char*>(t, l.at));
        if (!GetField(context, l.name, index))
            continue;
        struct {
            char* begin;
            char* end;
            char* capacity;
        } strings{nullptr, nullptr, nullptr};
        CallCdecl<void>(addr::SC4ScriptValue_StringVector, context, static_cast<void*>(&strings), -1);
        for (char* s = strings.begin; s != strings.end; s += 0x14) {
            // The string's STL part copied (the length capped at npos) into
            // an SC4String of the reader's own, read as hex.
            const char* const from = s ? At<const char*>(s, 4) : nullptr;
            const std::uint32_t length = static_cast<std::uint32_t>(At<const char*>(s, 8) - from);
            const std::uint32_t used = length < 0xFFFFFFFFu ? length : 0xFFFFFFFFu;
            GameString copy{0, nullptr, nullptr, nullptr, 0};
            std::uint32_t allocatorTag = 0;
            CallThis<void>(addr::cSC4Advice_F408420, &copy.begin, from, from + used, static_cast<void*>(&allocatorTag));
            copy.vtable = addr::SC4String_Vtable;
            copy.extra = 0;
            const char* const text = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&copy));
            const std::uint32_t id = CallCdecl<std::uint32_t>(addr::SC4_strtoul, text, static_cast<char**>(nullptr), 16);
            ListAppend(At<char*>(t, l.at), id);
            FreeString(copy);
        }
        Pop(context);
        for (char* s = strings.begin; s != strings.end; s += 0x14)
            Virtual<std::uint32_t>(s, 0x70, 0u);
        if (strings.begin)
            Free(strings.begin);
    }
    return 1;
}

// The behaviour record (0x7C bytes) the behaviour reader builds on its
// stack and destroys inline: +0x1C's buffer freed, +0x10's strings
// destroyed (slot 0x70 with 0) and its buffer freed.
void DestroyRecordInline(char* record)
{
    if (void* const buffer = At<void*>(record, 0x1C))
        Free(buffer);
    char* const begin = At<char*>(record, 0x10);
    char* const end = At<char*>(record, 0x14);
    for (char* s = begin; s != end; s += 0x14)
        Virtual<std::uint32_t>(s, 0x70, 0u);
    if (char* const buffer = At<char*>(record, 0x10))
        Free(buffer);
}

// 0x00522860 (index, template), ret 8: `behavior`, a list of behaviour
// tables, as the template's record vector (+0x38), emptied first and sorted
// after (0x00522800). See the address entry for the fields.
std::uint8_t __fastcall ReadBehaviours(char* self, void*, std::int32_t index, char* t)
{
    static bool logged;
    FirstCall("ReadBehaviours", logged);
    void* const context = Context(self);
    char** const vector = &At<char*>(t, 0x38);
    std::uint8_t tag = 0;
    {
        char* const begin = CallCdecl<char*>(addr::SC4BehaviourRecord_CopyForward, vector[1], vector[1], vector[0],
                                             static_cast<void*>(&tag), 0u);
        CallCdecl<void>(addr::cSC4AutomataControllerTemplate_DestroyRecords, begin, vector[1], static_cast<void*>(&tag));
        vector[1] = begin;
    }
    if (!GetField(context, addr::cSC4AutomataScriptSystem_Key_behavior, index))
        return 1;
    TableWalk walk;
    WalkAt(walk, context, -1);
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        TableWalk entry;
        WalkAt(entry, context, -1);
        const std::int32_t at = entry.Index();
        alignas(4) char record[0x7C];
        CallThis<char*>(addr::SC4BehaviourRecord_Construct, record);
        std::uint32_t state = 0;
        FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_state, &state, at);
        At<std::uint32_t>(record, 0) = state;
        if (GetField(context, addr::cSC4AutomataScriptSystem_Key_anims, at)) {
            CallCdecl<void>(addr::SC4ScriptValue_StringVector, context, static_cast<void*>(record + 0x10), -1);
            Pop(context);
        }
        if (GetField(context, addr::cSC4AutomataScriptSystem_Key_sfx, at)) {
            char* const head = static_cast<char*>(Allocate(0x1C));
            At<char*>(head, 0) = head;
            At<char*>(head, 4) = head;
            char* list = head;
            CallCdecl<void>(addr::SC4ScriptValue_StringList, context, static_cast<void*>(&list), -1);
            for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
                const char* const text = Virtual<const char*>(node + 8, 0x14);
                const std::uint32_t id =
                    CallCdecl<std::uint32_t>(addr::SC4_strtoul, text, static_cast<char**>(nullptr), 16);
                PushBack(At<U32Vector>(record, 0x1C), id);
            }
            Pop(context);
            ClearStringList(list);
            Free(list);
        }
        if (!FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_radius, record + 0x04, at))
            At<std::uint32_t>(record, 0x04) = 0x7F7FFFFFu; // FLT_MAX
        FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_priority, record + 0x0C, at);
        float percentage = 0.0f;
        if (!FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_percentage, &percentage, at))
            At<std::uint32_t>(&percentage, 0) = 0x3F800000u;
        At<float>(record, 0x08) = percentage;
        FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_timeout, record + 0x28, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_final, record + 0x2C, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_ignore_paths, record + 0x44, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_ignore_roads, record + 0x45, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_ignore_terrain, record + 0x46, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_ignore_attractors, record + 0x47, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_avoid_lots, record + 0x48, at);
        FieldBool(context, addr::cSC4AutomataScriptSystem_Key_avoid_roads, record + 0x49, at);
        At<std::uint32_t>(record, 0x30) = 0x3F800000u;
        At<std::uint32_t>(record, 0x34) = 0x3F800000u;
        CallCdecl<void>(addr::SC4ScriptField_FloatPair, context, Text(addr::cSC4AutomataScriptSystem_Key_kph_multiply),
                        static_cast<void*>(record + 0x30), at);
        CallCdecl<void>(addr::SC4ScriptField_Uint32Pair, context, Text(addr::cSC4AutomataScriptSystem_Key_kph),
                        static_cast<void*>(record + 0x38), at);
        FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_acceleration, record + 0x40, at);
        At<std::uint32_t>(record, 0x4C) = 0;
        FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_path_offset, record + 0x4C, at);
        if (GetField(context, addr::cSC4AutomataScriptSystem_Key_random_walk, at)) {
            TableWalk walkTable;
            WalkAt(walkTable, context, -1);
            const std::int32_t w = walkTable.Index();
            CallCdecl<void>(addr::SC4ScriptField_FloatPair, context, Text(addr::cSC4AutomataScriptSystem_Key_delay),
                            static_cast<void*>(record + 0x50), w);
            if (GetField(context, addr::cSC4AutomataScriptSystem_Key_turn, w)) {
                At<std::uint32_t>(record, 0x58) = 0;
                At<std::uint32_t>(record, 0x5C) = 0;
                if (Virtual<std::uint8_t>(context, 0x120, -1)) {
                    U32Vector values{nullptr, nullptr, nullptr};
                    CallCdecl<void>(addr::SC4ScriptValue_FloatVector, context, static_cast<void*>(&values), -1);
                    if (values.begin != values.end)
                        At<std::uint32_t>(record, 0x58) = values.begin[0];
                    if (static_cast<std::uint32_t>(values.end - values.begin) > 1)
                        At<std::uint32_t>(record, 0x5C) = values.begin[1];
                    if (values.begin)
                        Free(values.begin);
                } else if (Virtual<std::uint8_t>(context, 0x38, -1)) {
                    At<float>(record, 0x58) = static_cast<float>(Virtual<long double>(context, 0x5C, -1));
                }
                Pop(context);
            }
            CallCdecl<void>(addr::SC4ScriptField_FloatPair, context, Text(addr::cSC4AutomataScriptSystem_Key_strength),
                            static_cast<void*>(record + 0x60), w);
            CallCdecl<void>(addr::SC4ScriptField_FloatPair, context,
                            Text(addr::cSC4AutomataScriptSystem_Key_idle_delay), static_cast<void*>(record + 0x68), w);
            CallCdecl<void>(addr::SC4ScriptField_FloatPair, context, Text(addr::cSC4AutomataScriptSystem_Key_idle_time),
                            static_cast<void*>(record + 0x70), w);
            FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_deceleration, record + 0x78, w);
            CallCdecl<void>(addr::SC4ScriptField_Uint32Pair, context, Text(addr::cSC4AutomataScriptSystem_Key_kph),
                            static_cast<void*>(record + 0x38), w);
            FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_acceleration, record + 0x40, w);
            Pop(context);
            walkTable.DestroyString();
        }
        if (vector[1] != vector[2]) {
            if (vector[1])
                CallThis<char*>(addr::SC4BehaviourRecord_Copy, vector[1], static_cast<const char*>(record));
            vector[1] += 0x7C;
        } else {
            CallThis<void>(addr::SC4BehaviourRecord_VectorInsertOverflow, static_cast<void*>(vector), vector[1],
                           static_cast<const char*>(record), static_cast<void*>(&tag), 1u, 1u);
        }
        DestroyRecordInline(record);
        entry.DestroyString();
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    Pop(context);
    CallCdecl<void>(addr::SC4BehaviourRecord_Sort, At<char*>(t, 0x38), At<char*>(t, 0x3C));
    walk.DestroyString();
    return 1;
}

// The template name and template the two template readers share: the
// entry's key copied, a walk made for the value's index, the template of
// that name (or a new one of the class); a template of the other class gets
// the refusal message, formatted and dropped; the name copied in.
char* TemplateOf(char* self, void* context, GameString& name, TableWalk& entry, std::uint32_t classId,
                 std::uintptr_t format, std::uint8_t& tag)
{
    const char* const key = Virtual<const char*>(context, 0x60, -2);
    CopyCString(name, key);
    WalkAt(entry, context, -1);
    char* made = Virtual<char*>(self, 0x44, static_cast<void*>(&name));
    if (!made) {
        made = Virtual<char*>(self, 0x40, static_cast<void*>(&name), classId);
    } else if (At<std::uint32_t>(made, 0x18) != classId) {
        GameString message;
        NewEmptyString(message);
        const std::uint32_t existing = At<std::uint32_t>(made, 0x18);
        const char* const text = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&name));
        CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(&message), Text(format), text, existing);
        FreeString(message);
    }
    if (reinterpret_cast<char*>(&name.begin) != made + 8)
        CallThis<void>(addr::SC4String_AssignRange, made + 8, static_cast<const char*>(name.begin),
                       static_cast<const char*>(name.end), static_cast<void*>(&tag));
    return made;
}

void ReadAutomataNames(void* context, std::int32_t index, char* t, bool inlineClear)
{
    if (GetField(context, addr::cSC4AutomataScriptSystem_TableName, index)) {
        CallCdecl<void>(addr::SC4ScriptValue_StringList, context, static_cast<void*>(t + 0x84), -1);
        Pop(context);
    } else if (inlineClear) {
        ClearStringList(At<char*>(t, 0x84));
    } else {
        CallThis<void>(addr::SC4UI_F43ABA0, t + 0x84);
    }
}

// The template parts both readers call in order; false at the first that
// fails.
bool ReadCommon(char* self, std::int32_t index, char* t)
{
    using Part = std::uint8_t(__thiscall*)(void*, std::int32_t, char*);
    for (const std::uintptr_t part : {addr::cSC4AutomataScriptSystem_ReadTimeOfDay,
                                      addr::cSC4AutomataScriptSystem_ReadCalendar,
                                      addr::cSC4AutomataScriptSystem_ReadBehaviours,
                                      addr::cSC4AutomataScriptSystem_ReadTriggers})
        if (!reinterpret_cast<Part>(part)(self, index, t))
            return false;
    return true;
}

bool ReadPart(char* self, std::uintptr_t part, std::int32_t index, char* t)
{
    using Part = std::uint8_t(__thiscall*)(void*, std::int32_t, char*);
    return reinterpret_cast<Part>(part)(self, index, t) != 0;
}

// ====================================================================================
// The template readers

// 0x00523500: the global table `attractor` (0x00B09A3C), one attractor
// template per entry.
std::uint8_t __fastcall ReadAttractors(char* self, void*)
{
    static bool logged;
    FirstCall("ReadAttractors", logged);
    void* const context = Context(self);
    std::uint8_t ok = 1;
    TableWalk walk;
    WalkGlobal(walk, context, Global<const char*>(addr::cSC4AutomataScriptSystem_AttractorTableName));
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        GameString name;
        TableWalk entry;
        std::uint8_t& flag = ok;
        char* const t = TemplateOf(self, context, name, entry, kAttractorClass,
                                   addr::cSC4AutomataScriptSystem_AttractorExistsFormat, flag);
        const std::int32_t index = entry.Index();
        FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_radius, t + 0x20, index);
        PointVector points{nullptr, nullptr, nullptr};
        if (GetField(context, addr::cSC4AutomataScriptSystem_Key_strength, index)) {
            U32Vector values{nullptr, nullptr, nullptr};
            CallCdecl<void>(addr::SC4ScriptValue_FloatVector, context, static_cast<void*>(&values), -1);
            const std::uint32_t count = static_cast<std::uint32_t>(static_cast<std::int32_t>(
                                            reinterpret_cast<char*>(values.end) - reinterpret_cast<char*>(values.begin)) >>
                                        2);
            // The resize's fill value is a leftover local of the game's (the
            // key's end); every point is written after, so ours fills zeros.
            std::uint32_t fill[2] = {0, 0};
            const std::uint32_t size = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(reinterpret_cast<char*>(points.end) - reinterpret_cast<char*>(points.begin)) >> 3);
            if (count > 1) {
                if (count < size)
                    points.end = points.begin + 2 * count;
                else
                    CallThis<void>(addr::SC4Vector2Vector_InsertN, static_cast<void*>(&points), points.end,
                                   count - size, static_cast<const void*>(fill));
                const std::int32_t steps = static_cast<std::int32_t>(count - 1);
                double divisor = static_cast<double>(steps);
                if (steps < 0)
                    divisor += Global<float>(addr::SC4_FloatTwoTo32);
                const float step = static_cast<float>(static_cast<double>(At<float>(t, 0x20)) / divisor);
                double x = Global<float>(addr::SC4_FloatZero);
                for (std::uint32_t i = 0; i < count; ++i) {
                    points.begin[2 * i] = static_cast<float>(x);
                    At<std::uint32_t>(points.begin, 8 * i + 4) = values.begin[i];
                    x += step;
                }
            } else {
                if (size <= 2)
                    CallThis<void>(addr::SC4Vector2Vector_InsertN, static_cast<void*>(&points), points.end, 2 - size,
                                   static_cast<const void*>(fill));
                else
                    points.end = points.begin + 4;
                At<std::uint32_t>(points.begin, 0) = 0;
                At<std::uint32_t>(points.begin, 4) = values.begin[0];
                At<std::uint32_t>(points.begin, 8) = At<std::uint32_t>(t, 0x20);
                At<std::uint32_t>(points.begin, 12) = values.begin[0];
            }
            Pop(context);
            if (values.begin)
                Free(values.begin);
        } else {
            const std::uint32_t size = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(reinterpret_cast<char*>(points.end) - reinterpret_cast<char*>(points.begin)) >> 3);
            std::uint32_t fill[2] = {0, 0};
            if (size <= 2)
                CallThis<void>(addr::SC4Vector2Vector_InsertN, static_cast<void*>(&points), points.end, 2 - size,
                               static_cast<const void*>(fill));
            else
                points.end = points.begin + 4;
            At<std::uint32_t>(points.begin, 0) = 0;
            At<std::uint32_t>(points.begin, 4) = 0x42C80000u; // 100.0f
            At<std::uint32_t>(points.begin, 8) = At<std::uint32_t>(t, 0x20);
            At<std::uint32_t>(points.begin, 12) = 0;
        }
        CallThis<std::uint8_t>(addr::SC4Curve_SetPoints, t + 0x88, static_cast<const void*>(&points));
        bool good = false;
        if (ok) {
            ReadAutomataNames(context, index, t, true);
            if (ReadCommon(self, index, t)) {
                At<std::uint32_t>(t, 0x4C) = 0;
                FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_lifetime, t + 0x4C, index);
                At<std::uint32_t>(t, 0x50) = 0;
                FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_delay, t + 0x50, index);
                if (ReadPart(self, addr::cSC4AutomataScriptSystem_ReadFlags, index, t) &&
                    ReadPart(self, addr::cSC4AutomataScriptSystem_ReadSounds, index, t)) {
                    // ReadIgnoredStates (0x0051CD20), inline here.
                    EmptyVector(t + 0x6C);
                    if (GetField(context, addr::cSC4AutomataScriptSystem_Key_ignore_states, index)) {
                        CallCdecl<void>(addr::SC4ScriptValue_Uint32Vector, context, static_cast<void*>(t + 0x6C), -1);
                        Pop(context);
                    }
                    At<std::uint32_t>(t, 0x5C) = 0;
                    FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_exclusive_radius, t + 0x5C, index);
                    good = true;
                }
            }
        }
        ok = good ? 1 : 0;
        if (!FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_max_count, t + 0x54, index))
            At<std::uint32_t>(t, 0x54) = 0;
        if (points.begin)
            Free(points.begin);
        entry.DestroyString();
        FreeString(name);
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    Pop(context);
    walk.DestroyString();
    return ok;
}

// 0x00522F30: the global table `generator` (0x00B09A40), one generator
// template per entry.
std::uint8_t __fastcall ReadGenerators(char* self, void*)
{
    static bool logged;
    FirstCall("ReadGenerators", logged);
    void* const context = Context(self);
    std::uint8_t ok = 1;
    TableWalk walk;
    WalkGlobal(walk, context, Global<const char*>(addr::cSC4AutomataScriptSystem_GeneratorTableName));
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        GameString name;
        TableWalk entry;
        char* const t = TemplateOf(self, context, name, entry, kGeneratorClass,
                                   addr::cSC4AutomataScriptSystem_GeneratorExistsFormat, ok);
        const std::int32_t index = entry.Index();
        bool good = false;
        if (ok) {
            ReadAutomataNames(context, index, t, false);
            if (ReadCommon(self, index, t)) {
                At<std::uint32_t>(t, 0x4C) = 0;
                FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_lifetime, t + 0x4C, index);
                At<std::uint32_t>(t, 0x50) = 0;
                FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_delay, t + 0x50, index);
                if (ReadPart(self, addr::cSC4AutomataScriptSystem_ReadFlags, index, t) &&
                    ReadPart(self, addr::cSC4AutomataScriptSystem_ReadSounds, index, t) &&
                    ReadPart(self, addr::cSC4AutomataScriptSystem_ReadIgnoredStates, index, t)) {
                    At<std::uint32_t>(t, 0x5C) = 0;
                    FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_exclusive_radius, t + 0x5C, index);
                    good = true;
                }
            }
        }
        ok = good ? 1 : 0;
        if (GetField(context, addr::cSC4AutomataScriptSystem_Key_radius, index)) {
            if (Virtual<std::uint8_t>(context, 0x120, -1)) {
                U32Vector values{nullptr, nullptr, nullptr};
                CallCdecl<void>(addr::SC4ScriptValue_FloatVector, context, static_cast<void*>(&values), -1);
                At<std::uint32_t>(t, 0x20) = values.begin[0];
                At<std::uint32_t>(t, 0xA8) = values.begin[1];
                Free(values.begin);
            } else {
                const float radius = static_cast<float>(Virtual<long double>(context, 0x5C, -1));
                At<float>(t, 0x20) = radius;
                At<float>(t, 0xA8) = radius;
            }
            Pop(context);
        }
        struct Kind {
            std::uintptr_t name;
            std::uint32_t kind;
        };
        static constexpr Kind kKinds[] = {
            {addr::cSC4AutomataScriptSystem_Key_population_pct, 2},
            {addr::cSC4AutomataScriptSystem_Key_occupancy_pct, 1},
            {addr::cSC4AutomataScriptSystem_Key_count, 0},
        };
        bool kindFound = false;
        for (const Kind& k : kKinds)
            if (GetField(context, k.name, index)) {
                At<float>(t, 0x88) = static_cast<float>(Virtual<long double>(context, 0x5C, -1));
                At<std::uint32_t>(t, 0x8C) = k.kind;
                Pop(context);
                kindFound = true;
                break;
            }
        if (!kindFound) {
            At<std::uint32_t>(t, 0x88) = 0;
            At<std::uint32_t>(t, 0x8C) = 0;
        }
        if (!FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_max_count, t + 0x54, index))
            At<std::uint32_t>(t, 0x54) = 0;
        FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_rate, t + 0x98, index);
        std::uint32_t rateScale = 0;
        FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_rate_scale, &rateScale, index);
        At<std::uint32_t>(t, 0x9C) = rateScale;
        std::uint32_t event = 0;
        if (!FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_event, &event, index))
            event = 0;
        At<std::uint32_t>(t, 0xA0) = event;
        if (!FieldFloat(context, addr::cSC4AutomataScriptSystem_Key_random_chance, t + 0xA4, index))
            At<std::uint32_t>(t, 0xA4) = 0x3F800000u;
        At<std::uint32_t>(t, 0xAC) = 0;
        FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_lifetime_count, t + 0xAC, index);
        At<std::uint32_t>(t, 0x90) = 1;
        At<std::uint32_t>(t, 0x94) = 1;
        CallCdecl<void>(addr::SC4ScriptField_Uint32Pair, context, Text(addr::cSC4AutomataScriptSystem_Key_group_count),
                        static_cast<void*>(t + 0x90), index);
        std::uint32_t priority = 0;
        std::uint8_t byte;
        if (!FieldUint32(context, addr::cSC4AutomataScriptSystem_Key_priority, &priority, index))
            byte = 0x19;
        else
            byte = static_cast<std::uint8_t>(priority > 0xFF ? 0xFFu : priority);
        At<std::uint8_t>(t, 0xB0) = byte;
        entry.DestroyString();
        FreeString(name);
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    Pop(context);
    walk.DestroyString();
    return ok;
}

// ====================================================================================
// The loader

// 0x0051DAA0: for the attractor and generator templates of the template
// map, the automata group names (+0x84, SC4Strings) turned into the IDs of
// +0x78 (GetAutomataGroup by name, slot 7; the names not found dropped),
// the name list emptied.
std::uint8_t __fastcall ResolveTemplateGroups(char* self, void*)
{
    static bool logged;
    FirstCall("ResolveTemplateGroups", logged);
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, self + field::kTemplates, static_cast<void*>(&it));
    for (char* node = it.node; node; node = NextStringNode(node, it.table)) {
        char* const t = At<char*>(node, 0x10);
        const std::uint32_t classId = At<std::uint32_t>(t, 0x18);
        if (classId != kGeneratorClass && classId != kAttractorClass)
            continue;
        char* const head = At<char*>(t, 0x84);
        U32Vector& ids = At<U32Vector>(t, 0x78);
        EmptyVector(&ids);
        for (char* n = At<char*>(head, 0); n != At<char*>(t, 0x84); n = At<char*>(n, 0)) {
            char* const group = Virtual<char*>(self, 0x1C, static_cast<void*>(n + 8));
            if (group)
                PushBack(ids, At<std::uint32_t>(group, group::kId));
        }
        ClearStringList(At<char*>(t, 0x84));
    }
    return 1;
}

// 0x0051EEE0: the global table `track_buildings` (0x00B09A70): numbers, or
// strings read as hex, appended to the track building list (+0xA0) when
// not 0.
std::uint8_t __fastcall ReadTrackBuildings(char* self, void*)
{
    static bool logged;
    FirstCall("ReadTrackBuildings", logged);
    void* const context = Context(self);
    TableWalk walk;
    WalkGlobal(walk, context, Global<const char*>(addr::cSC4AutomataScriptSystem_TrackBuildingsName));
    CallThis<void>(addr::SC4ScriptTableWalk_First, walk.bytes);
    while (!walk.Done()) {
        std::uint32_t id = 0;
        bool read = false;
        if (Virtual<std::uint8_t>(Context(self), 0x38, -1)) {
            id = Virtual<std::uint32_t>(Context(self), 0x54, -1);
            read = true;
        } else if (Virtual<std::uint8_t>(Context(self), 0x3C, -1)) {
            const char* const text = Virtual<const char*>(Context(self), 0x60, -1);
            GameString copy{0, nullptr, nullptr, nullptr, 0};
            std::uint8_t tag = 0;
            CallThis<void>(addr::SC4String_FromRange, &copy.begin, text, text + std::strlen(text),
                           static_cast<void*>(&tag));
            copy.vtable = addr::SC4String_Vtable;
            copy.extra = 0;
            const char* const data = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&copy));
            id = CallCdecl<std::uint32_t>(addr::SC4_strtoul, data, static_cast<char**>(nullptr), 16);
            FreeString(copy);
            read = true;
        }
        if (read && id)
            ListAppend(At<char*>(self, field::kTrackGroups), id);
        CallThis<void>(addr::SC4ScriptTableWalk_Next, walk.bytes);
    }
    walk.DestroyString();
    return 1;
}

// 0x0051F5C0: for the groups of the name map (+0x4C): the parent reference
// (+0x4E) resolved (the table's `__group_handle` userdata into +0xC) and
// dropped; the own reference (+0x4C): its `__group_handle` field cleared
// and the reference dropped; then the member names (+0x54, SC4Strings)
// turned into the IDs of +0x50 (the name map's groups, ID not 0), the name
// list emptied.
std::uint8_t __fastcall ResolveGroups(char* self, void*)
{
    static bool logged;
    FirstCall("ResolveGroups", logged);
    char* const byName = self + field::kGroupsByName;
    const char* const handle = Global<const char*>(addr::cSC4AutomataScriptSystem_GroupHandleName);
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, byName, static_cast<void*>(&it));
    for (char* node = it.node; node; node = NextStringNode(node, it.table)) {
        char* const g = At<char*>(node, 0x10);
        const std::int16_t reference = At<std::int16_t>(g, group::kWord4E);
        if (reference == -2)
            continue;
        Virtual<std::uint32_t>(Context(self), 0xAC, static_cast<std::int32_t>(reference));
        if (!Virtual<std::uint8_t>(Context(self), 0x128, -1)) {
            const std::int32_t top = static_cast<std::int16_t>(Virtual<std::int32_t>(Context(self), 0x18));
            if (Virtual<std::uint8_t>(Context(self), 0x120, top) &&
                CallCdecl<std::uint8_t>(addr::SC4Script_GetField, Context(self), handle, top)) {
                At<void*>(g, group::kParent) = Virtual<void*>(Context(self), 0x70, -1);
                Pop(Context(self));
            }
            Pop(Context(self));
        }
        Virtual<std::uint32_t>(Context(self), 0xF8, static_cast<std::int32_t>(At<std::int16_t>(g, group::kWord4E)));
        At<std::uint16_t>(g, group::kWord4E) = 0xFFFEu;
    }
    for (char* node = FirstNode(byName); node; node = NextStringNode(node, byName)) {
        char* const g = At<char*>(node, 0x10);
        const std::int16_t reference = At<std::int16_t>(g, group::kWord4C);
        if (reference == -2)
            continue;
        Virtual<std::uint32_t>(Context(self), 0xAC, static_cast<std::int32_t>(reference));
        if (!Virtual<std::uint8_t>(Context(self), 0x128, -1)) {
            const std::int32_t top = static_cast<std::int16_t>(Virtual<std::int32_t>(Context(self), 0x18));
            if (Virtual<std::uint8_t>(Context(self), 0x120, top)) {
                Virtual<std::uint32_t>(Context(self), 0x84, handle);
                Virtual<std::uint32_t>(Context(self), 0x78);
                Virtual<std::uint32_t>(Context(self), 0xBC, top);
            }
            Pop(Context(self));
        }
        Virtual<std::uint32_t>(Context(self), 0xF8, static_cast<std::int32_t>(At<std::int16_t>(g, group::kWord4C)));
        At<std::uint16_t>(g, group::kWord4C) = 0xFFFEu;
    }
    for (char* node = FirstNode(byName); node; node = NextStringNode(node, byName)) {
        char* const g = At<char*>(node, 0x10);
        char* const head = At<char*>(g, 0x54);
        for (char* n = At<char*>(head, 0); n != At<char*>(g, 0x54); n = At<char*>(n, 0)) {
            // The member's name copied (the length capped at npos) to an
            // SC4String of the reader's own, looked up in the name map.
            const char* const from = At<const char*>(n + 8, 4);
            const std::uint32_t length = static_cast<std::uint32_t>(At<const char*>(n + 8, 8) - from);
            const std::uint32_t used = length < 0xFFFFFFFFu ? length : 0xFFFFFFFFu;
            GameString copy{0, nullptr, nullptr, nullptr, 0};
            const std::uint32_t size = used + 1;
            if (size == 0 || size > 0xFFFFFFFFu) {
                CallCdecl<void>(addr::STL_ThrowLengthError, Text(addr::STL_BasicStringText));
            } else {
                char* const buffer = static_cast<char*>(Allocate(size));
                copy.begin = buffer;
                copy.end = buffer;
                copy.capacity = buffer + size;
            }
            char* out = copy.begin;
            for (std::uint32_t i = 0; static_cast<std::int32_t>(i) < static_cast<std::int32_t>(used); ++i)
                *out++ = from[i];
            copy.end = out;
            *out = 0;
            copy.vtable = addr::SC4String_Vtable;
            copy.extra = 0;
            char* const found = CallThis<char*>(addr::SC4StringHashSet_Find, byName, static_cast<const void*>(&copy.begin));
            if (found) {
                if (char* const member = At<char*>(found, 0x10))
                    if (const std::uint32_t id = At<std::uint32_t>(member, group::kId))
                        ListAppend(At<char*>(g, group::kList50), id);
            }
            FreeString(copy);
        }
        ClearStringList(At<char*>(g, 0x54));
    }
    return 1;
}

// 0x0051FA60: the drive command exemplar (resource 0xA2E3D533, 0x6A231EAA,
// 0x0C0EAAD0 through the resource manager's slot 0x10, interface
// 0x42E3EA4B) read by a COM object (0x42E967BE, interface 0xE2C1B3C4; its
// slot 0xC takes the resource): properties 0 to 12, up to 8 values each,
// each value mapped to the property's index in the drive command map
// (+0xA4, emptied first).
std::uint8_t __fastcall ReadDriveCommands(char* self, void*)
{
    static bool logged;
    FirstCall("ReadDriveCommands", logged);
    char* const table = self + field::kDriveCommands;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, table);
    std::uint32_t key[3] = {0xA2E3D533u, 0x6A231EAAu, 0x0C0EAAD0u};
    void* resource = nullptr;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (Virtual<std::uint8_t>(resources, 0x10, static_cast<void*>(key), 0x42E3EA4Bu, static_cast<void**>(&resource), 0u,
                              0u)) {
        void* reader = nullptr;
        void* const com = CallCdecl<void*>(addr::RZGetCOM);
        if (void* const old = reader) { // never set: the game tests the holder it just zeroed
            reader = nullptr;
            Virtual<std::uint32_t>(old, 0x08);
        }
        if (Virtual<std::uint8_t>(com, 0x0C, 0x42E967BEu, 0xE2C1B3C4u, static_cast<void**>(&reader))) {
            Virtual<std::uint32_t>(resource, 0x0C, reader);
            for (std::uint32_t property = 0; property < 13; ++property) {
                std::uint32_t values[8];
                std::uint32_t count = 8;
                Virtual<std::uint32_t>(reader, 0x34, property, static_cast<std::uint32_t*>(values), &count);
                for (std::uint32_t i = 0; i < count; ++i) {
                    struct {
                        std::uint32_t key;
                        std::uint32_t value;
                    } pair{values[i], property};
                    CallThis<void>(addr::SC4Uint32HashSet_Resize, table, At<std::uint32_t>(table, 0x10) + 1);
                    alignas(4) char result[12];
                    CallThis<void*>(addr::SC4Uint32HashMap_Insert, table, static_cast<void*>(result),
                                    static_cast<const void*>(&pair));
                }
            }
        }
        if (reader)
            Virtual<std::uint32_t>(reader, 0x08);
    }
    if (resource)
        Virtual<std::uint32_t>(resource, 0x08);
    return 1;
}

// 0x00523B10: the loading (a stopwatch around it, read and dropped).
std::uint8_t __fastcall LoadScripts(char* self, void*)
{
    static bool logged;
    FirstCall("LoadScripts", logged);
    alignas(8) char watch[0x18];
    CallThis<void*>(addr::SC4Stopwatch_Construct, watch, 4u);
    CallThis<void*>(addr::SC4Stopwatch_ConstructMode, watch, 4u);
    CallThis<void>(addr::SC4Stopwatch_Start, watch);
    std::uint8_t result = 0;
    if (CallThis<std::uint8_t>(addr::cSC4AutomataScriptSystem_MakeLoaderContext, self)) {
        void* const context = Context(self);
        result = 1;
        const std::uint32_t error =
            CallCdecl<std::uint32_t>(addr::SC4_ScriptGroupLoad, context, 0x4A5E8F3Fu, 0u);
        if (!error) {
            using Reader = std::uint8_t(__thiscall*)(void*);
            const std::uintptr_t readers[] = {
                addr::cSC4AutomataScriptSystem_ReadAttractors, addr::cSC4AutomataScriptSystem_ReadGenerators,
                addr::cSC4AutomataScriptSystem_ReadGroups,     addr::cSC4AutomataScriptSystem_ReadVehicleKinds,
                addr::cSC4AutomataScriptSystem_ResolveTemplateGroups, addr::cSC4AutomataScriptSystem_ResolveGroups,
            };
            result = 1;
            for (const std::uintptr_t r : readers)
                if (!reinterpret_cast<Reader>(r)(self)) {
                    result = 0;
                    break;
                }
            reinterpret_cast<Reader>(addr::cSC4AutomataScriptSystem_ReadDriveCommands)(self);
            reinterpret_cast<Reader>(addr::cSC4AutomataScriptSystem_ReadTrackBuildings)(self);
        } else {
            GameString message;
            NewEmptyString(message);
            CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(&message),
                            Text(addr::cSC4AutomataScriptSystem_ConfigErrorFormat), error);
            FreeString(message);
        }
        At<std::uint8_t>(self, field::kLoadByte) = 0;
        if (void* const object = At<void*>(self, field::kObject18)) {
            Virtual<std::uint32_t>(object, 0x10);
            if (void* const again = At<void*>(self, field::kObject18)) {
                At<void*>(self, field::kObject18) = nullptr;
                Virtual<std::uint32_t>(again, 0x08);
            }
        }
    }
    CallThis<void>(addr::SC4Stopwatch_Stop, watch);
    float elapsed = 0.0f;
    CallThis<void>(addr::SC4Stopwatch_GetElapsed, watch, &elapsed);
    CallThis<void>(addr::SC4Stopwatch_Destruct, watch);
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

const Detour kLoader[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LoadScripts, LoadScripts),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ResolveTemplateGroups, ResolveTemplateGroups),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadTrackBuildings, ReadTrackBuildings),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ResolveGroups, ResolveGroups),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadDriveCommands, ReadDriveCommands),
};

const Detour kReaders[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadAttractors, ReadAttractors),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadGenerators, ReadGenerators),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadFlags, ReadFlags),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadCalendar, ReadCalendar),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadSounds, ReadSounds),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadIgnoredStates, ReadIgnoredStates),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadTimeOfDay, ReadTimeOfDay),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_InsertPoint, InsertPoint),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadTriggers, ReadTriggers),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_ReadBehaviours, ReadBehaviours),
};

#undef OPENSC4_DETOUR

bool InstallLoader(registry::Mode) { return InstallList(kLoader, sizeof kLoader / sizeof kLoader[0], "loader"); }
bool InstallReaders(registry::Mode) { return InstallList(kReaders, sizeof kReaders / sizeof kReaders[0], "readers"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.scripts.loader", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_LoadScripts,
                        "Our automata script system's loading of the scripts: LoadScripts, the templates' groups by "
                        "name, the groups' parents and members, the drive commands and the track buildings (5 "
                        "detours; part s3a, src/core/automata_scripts_3.cpp)",
                        false, InstallLoader}),
    registry::Register({"simulators.automata.scripts.readers", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_ReadAttractors,
                        "Our automata script system's template readers: the attractor and generator templates and "
                        "their parts (flags, calendar, sounds, ignored states, time of day curve and its point "
                        "insert, triggers, behaviours) (10 detours; part s3a, src/core/automata_scripts_3.cpp)",
                        false, InstallReaders}),
};

} // namespace
} // namespace opensc4::automata_scripts
