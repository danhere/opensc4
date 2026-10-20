// Our automata script system, part s2 (build 641): the controller
// templates (the base, attractor and generator template constructors and
// destructors, the factory by class, CreateControllerTemplate), the seven
// Lua functions Init registers, the helpers the automata managers and the
// U-Drive call (a group's cached ID list, its record lookup and the record
// constructor, destructor and copy, the Lua value to ID), and the behaviour
// records the scripts' loading builds (the 0x7C-byte record's constructor,
// copy and assignment, the vector insert, and the std::sort instantiation
// over them: insertion sorts, the introsort loop, the median, the
// partition, the heap fallback). The layout is in
// src/core/automata_scripts.h and the survey in docs/automata.md, "Part s".
// Calls between functions of the part go to the game's addresses, so each
// detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses, hex,
// eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.scripts.templates (9): kTemplates below.
//   simulators.automata.scripts.lua (7): kLua.
//   simulators.automata.scripts.helpers (6): kHelpers.
//   simulators.automata.scripts.records (21): kRecords.
//
// The behaviour records (0x7C bytes; constructor 0x0050BD40, destructor
// 0x0050BDE0, the game's): +0x04 a float and +0x0C a uint32 are the sort
// keys, +0x10 a vector of 0x14-byte objects (released through their slot
// 0x70 with 0), +0x1C a vector of uint32. The sort's comparator, inline
// at every compare, is "a before b" when a's float is below b's (an
// unordered pair is not below) or else a's uint32 is above b's. The
// functions that take a record by value (the insertions, the partition,
// the heap functions) destroy it before they return, as the MSVC ABI has
// the callee do; the game copy-constructs each by-value record into the
// argument area. Ours copy-constructs it into a local and passes the bytes
// (the callee owns them from there), so the allocations are the game's.
// The record copy that the partition and the heap functions destroy
// inline (the +0x1C buffer freed, the +0x10 objects' slot 0x70, its
// buffer freed) is done the same way here.
//
// The Lua functions are cdecl (a1, the arguments, the results, the
// binding): the binding's slot 0x20 with a1 and its slot 0x48 give the
// script system (null: nothing is done); the arguments' slot 0xC is the
// count and 0x14 an argument, whose slots 0x94 (a float), 0x64 (a uint32),
// 0x34 (a bool), 0xDC (an SC4String) and 0xF0 (an interface) read it; the
// results' slot 0x1C pushes. create_automaton (class, x, y, z, [flags,
// [three IDs, [a bool]]]) makes an automaton through 0x0050CDA0 and starts
// it (its slot 0x110); create_controller (template, x, y, z) asks the
// automata controller manager's slot 5 for a controller at a point;
// attach_controller (template, occupant) its slot 5, attach_controller_group
// (group, occupant) slot 10, detach_controller (template, occupant) slot 8,
// detach_controller_group (group, occupant) slot 12; get_source_building_count
// (key) asks the four services at 0x00B43D44.. in turn (slot 0x90) for the
// first nonzero answer. create_automaton, create_controller and
// attach_controller push the object they got as a script number,
// get_source_building_count its count; the group ones push nothing.
//
// Oddities kept: the template factory writes the class into a null
// template when the allocation fails (ours stores through the null pointer
// as the game does), and its refusal message is formatted
// and dropped; CreateControllerTemplate destroys a template whose name is
// taken; create_automaton reads its ninth argument and never uses it;
// the four functions with an occupant test and release an occupant slot
// they have just zeroed (never set); the record copy and assignment copy the fields
// one by one and leave the padding (+0x2D..+0x2F, +0x4A, +0x4B) alone; the
// record constructor zeroes the two vectors itself (no vector constructor); the three
// SC4String record (0x0051D040) leaves +0x3C alone, which its copy
// (0x0051FD30) copies; GroupCacheIds builds the parent chain's ID list in
// a vector, copies it to an operator_new block and hands that to the +0x18
// part (slot 0x198) only when that part is still empty.
//
// Stack slots the game reuses: get_source_building_count reads its key
// into the slot of its binding argument, so a key the argument's slot 0x64
// does not read stays the binding's address bits; ours starts its local
// from that address the same way. The Lua value to ID (0x0051D120) reads a
// number into its own argument's slot the same way (a failed read answers
// 0 there). create_automaton's flags start at 0x2000002 and get 2 when
// read; its bool and the occupant slots of the attach and detach functions
// are the game's argument slots, which nothing reads after; ours keep them
// in locals.
//
// Floats: the comparator's float compares only (x87 FCOMP, C0 and C2
// tested: below is C0 alone; unordered is not below and falls through to
// the uint32 compare). The positions the Lua
// functions read are floats read by the argument's slot 0x94, started from
// the float at 0x00B227D0.
//
// Still the game's, at their addresses: the record destructor 0x0050BDE0;
// the vectors' copy and assignment (0x0051FE90, 0x00520970, 0x00462F00,
// 0x00462C10); the range destroy 0x00518B20; SC4PtrVector::InsertN
// 0x0051CA60 and SC4PointVector::InsertN 0x007C61A0; SC4Curve::Init and
// SetPoints; the string helpers and strtoul; 0x0050CDA0; the controller
// manager's slots (parts a1, a2) and the template base's destructor
// 0x0051A9A0 (part a1).
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

// The game's SC4String on the stack (vtable 0x00A80810, begin, end,
// capacity, a fifth word zeroed).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

// An empty SC4String with an 8-byte buffer, as the game makes them on the
// stack: the body zeroed, the buffer allocated, its first byte 0, the
// vtable and the fifth word.
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

struct NamePair {
    char* begin;
    char* end;
    char* capacity;
    void* value;
};

struct InsertResult {
    void* node;
    void* table;
    std::uint8_t inserted;
    std::uint8_t pad[3];
};

const void* KeyOf(const void* name) { return name ? static_cast<const char*>(name) + 4 : nullptr; }

// As part s1's: (name, value) into a string hash map, the key copied into a
// pair on the stack, the buckets grown for one more, the insert, the
// pair's copy freed. Answers whether it went in.
bool InsertByName(char* self, std::size_t mapField, const void* name, void* value)
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
// The controller templates

constexpr std::uint32_t kAttractorClass = 0x6A1628BAu, kGeneratorClass = 0x4A1628C5u;

// 0x0051AB80, the template base's constructor.
char* __fastcall TemplateConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("TemplateConstruct", logged);
    At<std::uintptr_t>(self, 0x00) = addr::cSC4AutomataControllerTemplate_BaseVtable;
    At<std::uint32_t>(self, 0x08) = 0;
    At<std::uint32_t>(self, 0x0C) = 0;
    At<std::uint32_t>(self, 0x10) = 0;
    char* const buffer = static_cast<char*>(Allocate(8));
    At<char*>(self, 0x10) = buffer + 8;
    At<char*>(self, 0x08) = buffer;
    At<char*>(self, 0x0C) = buffer;
    *buffer = 0;
    At<std::uintptr_t>(self, 0x04) = addr::SC4String_Vtable;
    At<std::uint32_t>(self, 0x14) = 0;
    CallThis<void*>(addr::SC4Curve_Init, self + 0x24);
    At<std::uint32_t>(self, 0x38) = 0;
    At<std::uint32_t>(self, 0x3C) = 0;
    At<std::uint32_t>(self, 0x40) = 0;
    At<std::uint32_t>(self, 0x44) = 0;
    char* const first = static_cast<char*>(Allocate(0xC));
    At<char*>(first, 0) = first;
    At<char*>(first, 4) = first;
    At<char*>(self, 0x44) = first;
    At<std::uint32_t>(self, 0x48) = 0;
    char* const second = static_cast<char*>(Allocate(0xC));
    At<char*>(second, 0) = second;
    At<char*>(second, 4) = second;
    At<char*>(self, 0x48) = second;
    for (std::size_t at = 0x60; at <= 0x74; at += 4)
        At<std::uint32_t>(self, at) = 0;
    return self;
}

// The attractor's and generator's shared part after the base: the vector
// +0x78..+0x80, the list +0x84 (0x1C-byte nodes).
void DerivedFields(char* self, std::uintptr_t vtable)
{
    At<std::uintptr_t>(self, 0x00) = vtable;
    At<std::uint32_t>(self, 0x78) = 0;
    At<std::uint32_t>(self, 0x7C) = 0;
    At<std::uint32_t>(self, 0x80) = 0;
    At<std::uint32_t>(self, 0x84) = 0;
    char* const head = static_cast<char*>(Allocate(0x1C));
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    At<char*>(self, 0x84) = head;
}

// 0x0051B1F0, the attractor template's constructor (0x98 bytes): the base,
// the vtable, the vector and the list, its SC4Curve at +0x88.
char* __fastcall AttractorTemplateConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("AttractorTemplateConstruct", logged);
    CallThis<char*>(addr::cSC4AutomataControllerTemplate_Construct, self);
    DerivedFields(self, addr::cSC4AttractorTemplate_Vtable);
    CallThis<void*>(addr::SC4Curve_Init, self + 0x88);
    return self;
}

// 0x0051B240, the generator template's constructor (0xB4 bytes).
char* __fastcall GeneratorTemplateConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("GeneratorTemplateConstruct", logged);
    CallThis<char*>(addr::cSC4AutomataControllerTemplate_Construct, self);
    DerivedFields(self, addr::cSC4GeneratorTemplate_Vtable);
    return self;
}

void DestroyDerived(char* self)
{
    CallThis<void>(addr::SC4UI_F43ABA0, self + 0x84);
    if (void* const head = At<void*>(self, 0x84))
        Free(head);
    if (void* const buffer = At<void*>(self, 0x78))
        Free(buffer);
    CallThis<void>(addr::cSC4AutomataControllerTemplate_Destructor, self);
}

// 0x0051B2A0, the attractor template's destructor body: the curve's buffer
// (+0x88) freed (not destroyed), the list and the vector, the base's.
void __fastcall AttractorTemplateDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("AttractorTemplateDestroy", logged);
    if (void* const curve = At<void*>(self, 0x88))
        Free(curve);
    DestroyDerived(self);
}

// 0x0051B310, the generator template's destructor body.
void __fastcall GeneratorTemplateDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("GeneratorTemplateDestroy", logged);
    DestroyDerived(self);
}

// 0x0051B280 and 0x0051B2F0, the deleting destructors (slot 0), ret 4.
char* __fastcall AttractorTemplateDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("AttractorTemplateDeletingDestructor", logged);
    CallThis<void>(addr::cSC4AttractorTemplate_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

char* __fastcall GeneratorTemplateDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("GeneratorTemplateDeletingDestructor", logged);
    CallThis<void>(addr::cSC4GeneratorTemplate_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// 0x0051BDF0 (cdecl: name, class): a new template of the class (the
// generator's 0x4A1628C5, 0xB4 bytes; the attractor's 0x6A1628BA, 0x98
// bytes; else the refusal formatted and dropped, null), its class at
// +0x1C written without a null test, the name copied, +0x18 the class,
// +0x20 0, +0x34 -1, and its curve (+0x24) set to the points (0, 1) and
// (24, 1).
char* __cdecl TemplateFactory(const void* name, std::uint32_t classId)
{
    static bool logged;
    FirstCall("TemplateFactory", logged);
    char* made = nullptr;
    if (classId == kGeneratorClass) {
        void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0xB4));
        made = block ? CallThis<char*>(addr::cSC4GeneratorTemplate_Construct, block) : nullptr;
        At<std::uint32_t>(made, 0x1C) = kGeneratorClass;
    } else if (classId == kAttractorClass) {
        void* const block = CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::uint32_t>(0x98));
        made = block ? CallThis<char*>(addr::cSC4AttractorTemplate_Construct, block) : nullptr;
        At<std::uint32_t>(made, 0x1C) = kAttractorClass;
    } else {
        GameString message;
        NewEmptyString(message);
        CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(&message),
                        addr::cSC4AutomataScriptSystem_TemplateClassFormat, classId);
        FreeString(message);
        return nullptr;
    }
    const char* const source = static_cast<const char*>(KeyOf(name));
    if (source != made + 8) {
        std::uint32_t tag = classId; // the game passes the class argument's slot as the tag
        CallThis<void>(addr::SC4String_AssignRange, made + 8, At<const char*>(const_cast<char*>(source), 0),
                       At<const char*>(const_cast<char*>(source), 4), static_cast<void*>(&tag));
    }
    At<std::uint32_t>(made, 0x18) = classId;
    At<std::uint32_t>(made, 0x20) = 0;
    At<std::uint32_t>(made, 0x34) = 0xFFFFFFFFu;
    struct {
        float* begin;
        float* end;
        float* capacity;
    } points{nullptr, nullptr, nullptr};
    float point[2] = {0.0f, 1.0f};
    std::uint32_t tag = classId;
    CallThis<void>(addr::SC4PointVector_InsertN, static_cast<void*>(&points), static_cast<float*>(nullptr),
                   static_cast<const float*>(point), static_cast<void*>(&tag), 1u, 1u);
    point[0] = 24.0f;
    point[1] = 1.0f;
    if (points.end == points.capacity) {
        CallThis<void>(addr::SC4PointVector_InsertN, static_cast<void*>(&points), points.end,
                       static_cast<const float*>(point), static_cast<void*>(&tag), 1u, 1u);
    } else {
        if (points.end) {
            At<std::uint32_t>(points.end, 0) = 0x41C00000u; // 24.0f
            At<std::uint32_t>(points.end, 4) = 0x3F800000u; // 1.0f
        }
        points.end += 2;
    }
    CallThis<std::uint8_t>(addr::SC4Curve_SetPoints, made + 0x24, static_cast<const void*>(&points));
    if (points.begin)
        Free(points.begin);
    return made;
}

// 0x00520AC0, slot 16 (name, class), ret 8: a template from the factory
// into the template map (+0x8C); a name already there is refused (the
// message formatted and dropped, the template destroyed through its slot 0
// with 1, null).
char* __fastcall CreateControllerTemplate(char* self, void*, const void* name, std::uint32_t classId)
{
    static bool logged;
    FirstCall("CreateControllerTemplate", logged);
    char* const made = CallCdecl<char*>(addr::cSC4AutomataScriptSystem_TemplateFactory, name, classId);
    if (!made)
        return nullptr;
    if (InsertByName(self, field::kTemplates, name, made))
        return made;
    GameString message;
    NewEmptyString(message);
    const char* const text = Virtual<const char*>(const_cast<void*>(name), 0x14);
    CallCdecl<void>(addr::cRZBaseString_Sprintf, static_cast<void*>(&message),
                    addr::cSC4AutomataScriptSystem_TemplateExistsFormat, text);
    Virtual<void*>(made, 0x00, static_cast<std::uint32_t>(1));
    FreeString(message);
    return nullptr;
}

// ====================================================================================
// The Lua functions

// The binding's script system (slot 0x20 with a1, then 0x48), or null.
void* BoundSystem(void* a1, void* binding)
{
    void* const upvalue = Virtual<void*>(binding, 0x20, a1);
    return Virtual<void*>(binding, 0x48, upvalue);
}

void* Argument(void* arguments, std::uint32_t index) { return Virtual<void*>(arguments, 0x14, index); }

void ReadFloat(void* arguments, std::uint32_t index, float* out)
{
    Virtual<std::uint32_t>(Argument(arguments, index), 0x94, out);
}

// The script number the functions push, made in a buffer of their own
// (0x14 bytes, as the game's), pushed through the results' slot 0x1C.
void PushObject(void* results, void* object)
{
    alignas(4) char number[0x14];
    void* const made = CallThis<void*>(addr::SC4ScriptNumber_FromObject, number, object);
    Virtual<std::uint32_t>(results, 0x1C, 1u, made);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
}

void* ControllerManager() { return Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC); }

// 0x0051D120 (cdecl, a Lua value): a string (type 0x800E) read as hex
// (strtoul base 16); else the value's slot 0x64 into the argument's slot,
// 0 when it does not read.
std::uint32_t __cdecl LuaToId(void* value)
{
    static bool logged;
    FirstCall("LuaToId", logged);
    if (Virtual<std::uint32_t>(value, 0x0C) == 0x800Eu) {
        GameString text;
        NewEmptyString(text);
        Virtual<std::uint32_t>(value, 0xDC, static_cast<void*>(&text));
        const char* const data = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&text));
        const std::uint32_t id = CallCdecl<std::uint32_t>(addr::SC4_strtoul, data, static_cast<char**>(nullptr), 16);
        FreeString(text);
        return id;
    }
    std::uint32_t slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
    const std::uint8_t read = Virtual<std::uint8_t>(value, 0x64, &slot);
    return read ? slot : 0u;
}

// 0x0051D220, create_automaton.
std::uint8_t __cdecl LuaCreateAutomaton(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaCreateAutomaton", logged);
    void* made = nullptr;
    if (BoundSystem(a1, binding)) {
        const std::uint32_t count = Virtual<std::uint32_t>(arguments, 0x0C);
        const float zero = Global<float>(addr::cSC4AutomataScriptSystem_LuaFloatDefault);
        float position[3] = {zero, zero, zero};
        std::uint32_t flags = 0x2000002u;
        std::uint32_t ids[3] = {0, 0, 0};
        std::uint8_t unused = 0;
        const std::uint32_t automatonClass =
            CallCdecl<std::uint32_t>(addr::cSC4AutomataScriptSystem_LuaToId, Argument(arguments, 0));
        ReadFloat(arguments, 1, &position[0]);
        ReadFloat(arguments, 2, &position[1]);
        ReadFloat(arguments, 3, &position[2]);
        if (count > 4) {
            Virtual<std::uint32_t>(Argument(arguments, 4), 0x64, &flags);
            flags |= 2;
        }
        if (count > 7) {
            ids[0] = CallCdecl<std::uint32_t>(addr::cSC4AutomataScriptSystem_LuaToId, Argument(arguments, 5));
            ids[1] = CallCdecl<std::uint32_t>(addr::cSC4AutomataScriptSystem_LuaToId, Argument(arguments, 6));
            ids[2] = CallCdecl<std::uint32_t>(addr::cSC4AutomataScriptSystem_LuaToId, Argument(arguments, 7));
        }
        if (count > 8)
            Virtual<std::uint32_t>(Argument(arguments, 8), 0x34, &unused);
        made = CallCdecl<void*>(addr::cSC4AutomataManager_CreateByClass, automatonClass,
                                static_cast<const float*>(position), flags, static_cast<const std::uint32_t*>(ids), 0u,
                                0u);
        if (made)
            Virtual<std::uint32_t>(made, 0x110);
    }
    PushObject(results, made);
    return 1;
}

// 0x0051D3C0, create_controller.
std::uint8_t __cdecl LuaCreateController(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaCreateController", logged);
    void* const bound = BoundSystem(a1, binding);
    void* const controllers = ControllerManager();
    void* made = nullptr;
    if (bound && controllers) {
        GameString name;
        name.begin = name.end = name.capacity = nullptr;
        char* const buffer = static_cast<char*>(Allocate(8));
        name.capacity = buffer + 8;
        name.begin = buffer;
        name.end = buffer;
        *buffer = 0;
        const float zero = Global<float>(addr::cSC4AutomataScriptSystem_LuaFloatDefault);
        float position[3] = {zero, zero, zero};
        name.vtable = addr::SC4String_Vtable;
        name.extra = 0;
        Virtual<std::uint32_t>(Argument(arguments, 0), 0xDC, static_cast<void*>(&name));
        ReadFloat(arguments, 1, &position[0]);
        ReadFloat(arguments, 2, &position[1]);
        ReadFloat(arguments, 3, &position[2]);
        made = Virtual<void*>(controllers, 0x14, static_cast<void*>(nullptr), static_cast<void*>(&name),
                              static_cast<const float*>(position));
        FreeString(name);
    }
    PushObject(results, made);
    return 1;
}

// The attach and detach functions' shared part: the name (argument 0) and
// the occupant (argument 1, slot 0xF0 with 0x26BB767E), then `call` with
// them when the occupant is there, the occupant released after. Answers
// what the call answered (null when it was not made).
template <typename Call>
void* WithNameAndOccupant(void* arguments, Call&& call)
{
    GameString name;
    name.begin = name.end = name.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    name.capacity = buffer + 8;
    name.begin = buffer;
    name.end = buffer;
    *buffer = 0;
    name.vtable = addr::SC4String_Vtable;
    name.extra = 0;
    void* occupant = nullptr; // the game's binding argument slot, zeroed
    Virtual<std::uint32_t>(Argument(arguments, 0), 0xDC, static_cast<void*>(&name));
    void* const value = Argument(arguments, 1);
    if (void* const old = occupant) { // never set: the game tests the slot it just zeroed
        occupant = nullptr;
        Virtual<std::uint32_t>(old, 0x08);
    }
    Virtual<std::uint32_t>(value, 0xF0, 0x26BB767Eu, &occupant);
    void* answer = nullptr;
    if (occupant) {
        answer = call(&name, occupant);
        if (occupant)
            Virtual<std::uint32_t>(occupant, 0x08);
    }
    FreeString(name);
    return answer;
}

// 0x0051D510, attach_controller: the controller manager's slot 5 with
// (occupant, name, null); the controller pushed.
std::uint8_t __cdecl LuaAttachController(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaAttachController", logged);
    void* const bound = BoundSystem(a1, binding);
    void* const controllers = ControllerManager();
    void* made = nullptr;
    if (bound && controllers)
        made = WithNameAndOccupant(arguments, [&](GameString* name, void* occupant) {
            return Virtual<void*>(controllers, 0x14, occupant, static_cast<void*>(name), static_cast<const float*>(nullptr));
        });
    PushObject(results, made);
    return 1;
}

// 0x0051D640, attach_controller_group: slot 10 with (name, occupant, 0).
std::uint8_t __cdecl LuaAttachControllerGroup(void* a1, void* arguments, void*, void* binding)
{
    static bool logged;
    FirstCall("LuaAttachControllerGroup", logged);
    void* const bound = BoundSystem(a1, binding);
    void* const controllers = ControllerManager();
    if (bound && controllers)
        WithNameAndOccupant(arguments, [&](GameString* name, void* occupant) {
            Virtual<std::uint32_t>(controllers, 0x28, static_cast<void*>(name), occupant, 0u);
            return static_cast<void*>(nullptr);
        });
    return 1;
}

// 0x0051D750, detach_controller: slot 8 with (occupant, name).
std::uint8_t __cdecl LuaDetachController(void* a1, void* arguments, void*, void* binding)
{
    static bool logged;
    FirstCall("LuaDetachController", logged);
    void* const bound = BoundSystem(a1, binding);
    void* const controllers = ControllerManager();
    if (bound && controllers)
        WithNameAndOccupant(arguments, [&](GameString* name, void* occupant) {
            Virtual<std::uint32_t>(controllers, 0x20, occupant, static_cast<void*>(name));
            return static_cast<void*>(nullptr);
        });
    return 1;
}

// 0x0051D860, detach_controller_group: slot 12 with (name, occupant).
std::uint8_t __cdecl LuaDetachControllerGroup(void* a1, void* arguments, void*, void* binding)
{
    static bool logged;
    FirstCall("LuaDetachControllerGroup", logged);
    void* const bound = BoundSystem(a1, binding);
    void* const controllers = ControllerManager();
    if (bound && controllers)
        WithNameAndOccupant(arguments, [&](GameString* name, void* occupant) {
            Virtual<std::uint32_t>(controllers, 0x30, static_cast<void*>(name), occupant);
            return static_cast<void*>(nullptr);
        });
    return 1;
}

// 0x0051C7D0, get_source_building_count: the key (argument 0, slot 0x64)
// to the four services at 0x00B43D44.. in turn (slot 0x90), the first
// nonzero answer pushed as a number.
std::uint8_t __cdecl LuaGetSourceBuildingCount(void* a1, void* arguments, void* results, void* binding)
{
    static bool logged;
    FirstCall("LuaGetSourceBuildingCount", logged);
    void* const bound = BoundSystem(a1, binding);
    void* const services[4] = {Global<void*>(addr::SC4_Service43D44), Global<void*>(addr::SC4_Service43D48),
                               Global<void*>(addr::SC4_Service43D4C), Global<void*>(addr::SC4_Service43D50)};
    std::uint32_t count = 0;
    if (bound) {
        // The game reads the key into its binding argument's slot.
        std::uint32_t key = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(binding));
        Virtual<std::uint32_t>(Argument(arguments, 0), 0x64, &key);
        for (std::uint32_t i = 0; i < 4; ++i) {
            if (!services[i])
                continue;
            count = Virtual<std::uint32_t>(services[i], 0x90, key);
            if (count)
                break;
        }
    }
    alignas(4) char number[0x14];
    void* const made = CallThis<void*>(addr::SC4ScriptNumber_FromUint, number, count);
    Virtual<std::uint32_t>(results, 0x1C, 1u, made);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
    return 1;
}

// ====================================================================================
// The helpers the managers and the U-Drive call

// 0x0051CD90 (this = a group): unless the +0x18 part already has IDs (its
// slot 0x10), the IDs (+0x10) of the group and its parents, the nonzero
// ones in order, gathered in a vector (SC4PtrVector::InsertN when full),
// copied to an operator_new block and given to the part's slot 0x198
// (block, count); the vector's buffer freed. Answers the part.
void* __fastcall GroupCacheIds(char* self, void*)
{
    static bool logged;
    FirstCall("GroupCacheIds", logged);
    char* const part = self + group::kNumber;
    if (Virtual<std::uint32_t>(part, 0x10))
        return part;
    struct {
        std::uint32_t* begin;
        std::uint32_t* end;
        std::uint32_t* capacity;
    } ids{nullptr, nullptr, nullptr};
    std::uint32_t count = 0;
    for (char* g = self; g; g = At<char*>(g, group::kParent)) {
        std::uint32_t id = At<std::uint32_t>(g, group::kId);
        if (!id)
            continue;
        if (ids.end == ids.capacity) {
            std::uint8_t tag = 0;
            CallThis<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(&ids), ids.end,
                           static_cast<const std::uint32_t*>(&id), static_cast<void*>(&tag), 1u, 1u);
        } else {
            if (ids.end)
                *ids.end = id;
            ++ids.end;
        }
        ++count;
    }
    if (count) {
        std::uint32_t* const block =
            static_cast<std::uint32_t*>(CallCdecl<void*>(addr::SC4_operator_new, count * 4));
        for (std::uint32_t i = 0; i < count; ++i)
            block[i] = ids.begin[i];
        Virtual<std::uint32_t>(part, 0x198, block, count);
        CallCdecl<void>(addr::SC4_operator_delete, static_cast<void*>(block));
    }
    if (ids.begin)
        Free(ids.begin);
    return part;
}

void ConstructString(char* s)
{
    At<std::uint32_t>(s, 4) = 0;
    At<std::uint32_t>(s, 8) = 0;
    At<std::uint32_t>(s, 0xC) = 0;
    char* const buffer = static_cast<char*>(Allocate(8));
    At<char*>(s, 4) = buffer;
    At<char*>(s, 8) = buffer;
    At<char*>(s, 0xC) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(s, 0) = addr::SC4String_Vtable;
    At<std::uint32_t>(s, 0x10) = 0;
}

// 0x0051D040, the three-string record's constructor (SC4Strings at +0,
// +0x14, +0x28; +0x3C left alone).
char* __fastcall Record3Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Record3Construct", logged);
    ConstructString(self);
    ConstructString(self + 0x14);
    ConstructString(self + 0x28);
    return self;
}

// 0x0051D0D0, its destructor: the buffers freed, last string first.
void __fastcall Record3Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Record3Destroy", logged);
    for (const std::size_t at : {std::size_t{0x28}, std::size_t{0x14}, std::size_t{0}}) {
        At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(self, at + 4))
            Free(buffer);
    }
}

// 0x0051FD30 (from), ret 4: each string assigned from the other's unless
// it is itself (the first's source tested for null), +0x3C copied.
char* __fastcall Record3Assign(char* self, void*, char* from)
{
    static bool logged;
    FirstCall("Record3Assign", logged);
    for (const std::size_t at : {std::size_t{0}, std::size_t{0x14}, std::size_t{0x28}}) {
        char* const source = (at == 0 && !from) ? nullptr : from + at + 4;
        if (source != self + at + 4) {
            std::uint32_t tag = reinterpret_cast<std::uintptr_t>(from) & 0xFFFFFFFFu; // the argument's slot
            CallThis<void>(addr::SC4String_AssignRange, self + at + 4, At<const char*>(source, 0),
                           At<const char*>(source, 4), static_cast<void*>(&tag));
        }
    }
    At<std::uint32_t>(self, 0x3C) = At<std::uint32_t>(from, 0x3C);
    return self;
}

// 0x005204D0 (this = a group; key byte, out), ret 8: the record of the
// key in the record map (+0x70; bucket key modulo the count, the key byte
// at node + 4) assigned to *out, AL 1; AL 0 when it is not there.
std::uint8_t __fastcall GroupFindRecord(char* self, void*, std::uint32_t key, char* out)
{
    static bool logged;
    FirstCall("GroupFindRecord", logged);
    const std::uint8_t k = static_cast<std::uint8_t>(key);
    char* const table = self + group::kRecords;
    const std::int32_t bytes = static_cast<std::int32_t>(At<char*>(table, map::kEnd) - At<char*>(table, map::kBegin));
    const std::uint32_t buckets = static_cast<std::uint32_t>(bytes >> 2);
    for (char* node = At<char**>(table, map::kBegin)[k % buckets]; node; node = At<char*>(node, 0))
        if (At<std::uint8_t>(node, 4) == k) {
            CallThis<char*>(addr::cSC4OccupantGroupTemplate_Record3Assign, out, node + 8);
            return 1;
        }
    return 0;
}

// ====================================================================================
// The behaviour records

constexpr std::size_t kRecordSize = 0x7C;

// A record's bytes, passed by value as the game passes them.
struct RecordBytes {
    alignas(4) unsigned char bytes[kRecordSize];
};

std::int32_t RecordCount(const char* first, const char* last)
{
    return static_cast<std::int32_t>(last - first) / static_cast<std::int32_t>(kRecordSize);
}

// "a before b", the sort's comparator inline at every compare.
bool Before(const void* a, const void* b)
{
    const float af = At<float>(const_cast<void*>(a), 4);
    const float bf = At<float>(const_cast<void*>(b), 4);
    if (af < bf)
        return true;
    return At<std::uint32_t>(const_cast<void*>(a), 0xC) > At<std::uint32_t>(const_cast<void*>(b), 0xC);
}

void CopyRecord(void* to, const void* from)
{
    CallThis<void*>(addr::SC4BehaviourRecord_Copy, to, from);
}

void AssignRecord(void* to, const void* from)
{
    CallThis<void*>(addr::SC4BehaviourRecord_Assign, to, from);
}

void DestroyRecord(void* record) { CallThis<void>(addr::SC4BehaviourRecord_Destroy, record); }

// The record destroy the partition and the heap functions inline: +0x1C's
// buffer freed, each 0x14-byte object of +0x10's slot 0x70 with 0, +0x10's
// buffer freed.
void DestroyRecordInline(char* record)
{
    if (void* const buffer = At<void*>(record, 0x1C))
        Free(buffer);
    char* const begin = At<char*>(record, 0x10);
    char* const end = At<char*>(record, 0x14);
    for (char* object = begin; object != end; object += 0x14)
        Virtual<std::uint32_t>(object, 0x70, 0u);
    if (char* const buffer = At<char*>(record, 0x10))
        Free(buffer);
}

RecordBytes CopyOf(const void* from)
{
    RecordBytes copy;
    CopyRecord(copy.bytes, from);
    return copy;
}

using ByValueInsert = void(__cdecl*)(char*, RecordBytes, std::uint32_t);
using ByValueLinearInsert = void(__cdecl*)(char*, char*, RecordBytes, std::uint32_t);
using ByValueHeap = void(__cdecl*)(char*, std::int32_t, std::int32_t, RecordBytes, std::uint32_t);
using ByValuePartition = char*(__cdecl*)(char*, char*, RecordBytes, std::uint32_t);

// 0x0050BD40, the record's constructor.
char* __fastcall RecordConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("RecordConstruct", logged);
    At<std::uint32_t>(self, 0x08) = 0x3F800000u;
    for (const std::size_t at : {0x10u, 0x14u, 0x18u, 0x1Cu, 0x20u, 0x24u, 0x00u, 0x04u})
        At<std::uint32_t>(self, at) = 0;
    At<std::uint32_t>(self, 0x0C) = 1;
    At<std::uint32_t>(self, 0x28) = 0;
    At<std::uint8_t>(self, 0x2C) = 0;
    for (std::size_t at = 0x44; at <= 0x49; ++at)
        At<std::uint8_t>(self, at) = 0;
    At<std::uint32_t>(self, 0x4C) = 0;
    At<std::uint32_t>(self, 0x40) = 0x3F000000u;
    At<std::uint32_t>(self, 0x38) = 0xFFFFFFFFu;
    At<std::uint32_t>(self, 0x3C) = 0xFFFFFFFFu;
    At<std::uint32_t>(self, 0x50) = 0x41200000u;
    At<std::uint32_t>(self, 0x54) = 0x41700000u;
    At<std::uint32_t>(self, 0x58) = 0x3DCCCCCDu;
    At<std::uint32_t>(self, 0x5C) = 0x3E4CCCCDu;
    At<std::uint32_t>(self, 0x68) = 0;
    At<std::uint32_t>(self, 0x6C) = 0;
    At<std::uint32_t>(self, 0x70) = 0x40A00000u;
    At<std::uint32_t>(self, 0x74) = 0x41200000u;
    At<std::uint32_t>(self, 0x60) = 0x3F800000u;
    At<std::uint32_t>(self, 0x64) = 0x3F800000u;
    At<std::uint32_t>(self, 0x78) = 0x3F000000u;
    At<std::uint32_t>(self, 0x30) = 0x3F800000u;
    At<std::uint32_t>(self, 0x34) = 0x3F800000u;
    return self;
}

// The fields both the copy and the assignment copy one by one, after the
// two vectors.
void CopyFields(char* to, const char* from)
{
    char* const f = const_cast<char*>(from);
    At<std::uint32_t>(to, 0x28) = At<std::uint32_t>(f, 0x28);
    At<std::uint8_t>(to, 0x2C) = At<std::uint8_t>(f, 0x2C);
    for (std::size_t at = 0x30; at <= 0x40; at += 4)
        At<std::uint32_t>(to, at) = At<std::uint32_t>(f, at);
    for (std::size_t at = 0x44; at <= 0x49; ++at)
        At<std::uint8_t>(to, at) = At<std::uint8_t>(f, at);
    for (std::size_t at = 0x4C; at <= 0x78; at += 4)
        At<std::uint32_t>(to, at) = At<std::uint32_t>(f, at);
}

void CopyHead(char* to, const char* from)
{
    char* const f = const_cast<char*>(from);
    At<std::uint32_t>(to, 0x0) = At<std::uint32_t>(f, 0x0);
    At<std::uint32_t>(to, 0x4) = At<std::uint32_t>(f, 0x4);
    At<std::uint32_t>(to, 0x8) = At<std::uint32_t>(f, 0x8);
    At<std::uint32_t>(to, 0xC) = At<std::uint32_t>(f, 0xC);
}

// 0x005205D0 (from), ret 4: the copy constructor.
char* __fastcall RecordCopy(char* self, void*, const char* from)
{
    static bool logged;
    FirstCall("RecordCopy", logged);
    CopyHead(self, from);
    CallThis<void*>(addr::SC4ObjectVector14_Copy, self + 0x10, from + 0x10);
    CallThis<void*>(addr::SC4Uint32Vector_Copy, self + 0x1C, from + 0x1C);
    CopyFields(self, from);
    return self;
}

// 0x00521470 (from), ret 4: the assignment.
char* __fastcall RecordAssign(char* self, void*, const char* from)
{
    static bool logged;
    FirstCall("RecordAssign", logged);
    CopyHead(self, from);
    CallThis<void*>(addr::SC4ObjectVector14_Assign, self + 0x10, from + 0x10);
    CallThis<void*>(addr::SC4Uint32Vector_CopyAssign, self + 0x1C, from + 0x1C);
    CopyFields(self, from);
    return self;
}

// 0x00521350 (this = a record vector; position, value, unused, count, at
// the end), ret 0x14: the insert when the vector is full. A new buffer for
// the old count plus the larger of it and the count; the records before
// the position copied, the value copied count times, the records after it
// (unless at the end, 0x00520940), the old range destroyed (0x00518B20)
// and freed.
void __fastcall RecordVectorInsertOverflow(char** self, void*, char* position, const char* value, std::uint32_t,
                                           std::uint32_t count, std::uint8_t atEnd)
{
    static bool logged;
    FirstCall("RecordVectorInsertOverflow", logged);
    const std::uint32_t old = static_cast<std::uint32_t>(RecordCount(self[0], self[1]));
    const std::uint32_t grown = old + (old < count ? count : old);
    char* const buffer = grown ? static_cast<char*>(Allocate(grown * static_cast<std::uint32_t>(kRecordSize))) : nullptr;
    char* out = buffer;
    for (char* from = self[0]; from != position; from += kRecordSize) {
        if (out)
            CallThis<char*>(addr::SC4BehaviourRecord_Copy, out, from);
        out += kRecordSize;
    }
    if (count == 1) {
        if (out)
            CallThis<char*>(addr::SC4BehaviourRecord_Copy, out, value);
        out += kRecordSize;
    } else {
        for (std::uint32_t i = count; i; --i) {
            if (out)
                CallThis<char*>(addr::SC4BehaviourRecord_Copy, out, value);
            out += kRecordSize;
        }
    }
    std::uint32_t tag = atEnd;
    if (!atEnd)
        out = CallCdecl<char*>(addr::SC4BehaviourRecord_UninitializedCopy, position, self[1], out,
                               static_cast<void*>(&tag));
    CallCdecl<void>(addr::cSC4AutomataControllerTemplate_DestroyRecords, self[0], self[1], static_cast<void*>(&tag));
    if (self[0])
        Free(self[0]);
    self[1] = out;
    self[2] = buffer + grown * kRecordSize;
    self[0] = buffer;
}

// 0x00520940 (cdecl: first, last, destination, tag): each record
// copy-constructed into the destination (skipped, still counted, for a
// null destination); answers the destination's end.
char* __cdecl RecordUninitializedCopy(const char* first, const char* last, char* to, void*)
{
    static bool logged;
    FirstCall("RecordUninitializedCopy", logged);
    for (; first != last; first += kRecordSize, to += kRecordSize)
        if (to)
            CallThis<char*>(addr::SC4BehaviourRecord_Copy, to, first);
    return to;
}

// 0x00521620 (cdecl: first, last, destination end): copy_backward through
// the assignment; answers the destination's new start.
char* __cdecl RecordCopyBackward(const char* first, const char* last, char* to)
{
    static bool logged;
    FirstCall("RecordCopyBackward", logged);
    std::int32_t n = RecordCount(first, last);
    if (n <= 0)
        return to;
    for (; n; --n) {
        last -= kRecordSize;
        to -= kRecordSize;
        AssignRecord(to, last);
    }
    return to;
}

// 0x00521B20 (cdecl: first, last, destination): copy through the
// assignment; answers the destination's end.
char* __cdecl RecordCopyForward(const char* first, const char* last, char* to)
{
    static bool logged;
    FirstCall("RecordCopyForward", logged);
    std::int32_t n = RecordCount(first, last);
    if (n <= 0)
        return to;
    for (; n; --n) {
        AssignRecord(to, first);
        first += kRecordSize;
        to += kRecordSize;
    }
    return to;
}

// 0x00521550 (cdecl: last, the value by value, extra): the unguarded
// linear insert; the value destroyed.
void __cdecl RecordUnguardedLinearInsert(char* last, RecordBytes value, std::uint32_t)
{
    static bool logged;
    FirstCall("RecordUnguardedLinearInsert", logged);
    char* at = last;
    char* previous = last - kRecordSize;
    while (Before(value.bytes, previous)) {
        AssignRecord(at, previous);
        at = previous;
        previous -= kRecordSize;
    }
    AssignRecord(at, value.bytes);
    DestroyRecord(value.bytes);
}

// 0x005223C0 (cdecl: first, last, the value by value, extra): the linear
// insert; the value destroyed.
void __cdecl RecordLinearInsert(char* first, char* last, RecordBytes value, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordLinearInsert", logged);
    if (Before(value.bytes, first)) {
        std::uint32_t tag = 0;
        CallCdecl<char*>(addr::SC4BehaviourRecord_CopyBackward, static_cast<const char*>(first),
                         static_cast<const char*>(last), last + kRecordSize, static_cast<void*>(&tag), 0u);
        AssignRecord(first, value.bytes);
    } else {
        reinterpret_cast<ByValueInsert>(addr::SC4BehaviourRecord_UnguardedLinearInsert)(last, CopyOf(value.bytes), extra);
    }
    DestroyRecord(value.bytes);
}

// 0x00522540 (cdecl: first, last, extra): the insertion sort.
void __cdecl RecordInsertionSort(char* first, char* last, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordInsertionSort", logged);
    if (first == last)
        return;
    for (char* i = first + kRecordSize; i != last; i += kRecordSize)
        reinterpret_cast<ByValueLinearInsert>(addr::SC4BehaviourRecord_LinearInsert)(first, i, CopyOf(i), extra);
}

// 0x00521B70 (cdecl: first, last, unused, extra): the unguarded insertion
// sort.
void __cdecl RecordUnguardedInsertionSort(char* first, char* last, std::uint32_t, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordUnguardedInsertionSort", logged);
    for (char* i = first; i != last; i += kRecordSize)
        reinterpret_cast<ByValueInsert>(addr::SC4BehaviourRecord_UnguardedLinearInsert)(i, CopyOf(i), extra);
}

// 0x005226C0 (cdecl: first, last, extra): the final insertion sort (the
// first 16 guarded, the rest unguarded).
void __cdecl RecordFinalInsertionSort(char* first, char* last, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordFinalInsertionSort", logged);
    if (RecordCount(first, last) > 16) {
        char* const sixteen = first + 16 * kRecordSize;
        CallCdecl<void>(addr::SC4BehaviourRecord_InsertionSort, first, sixteen, extra);
        CallCdecl<void>(addr::SC4BehaviourRecord_UnguardedInsertionSort, sixteen, last, 0u, extra);
        return;
    }
    CallCdecl<void>(addr::SC4BehaviourRecord_InsertionSort, first, last, extra);
}

// 0x0051C8B0 (cdecl: a, b, c, extra): the median of three by the
// comparator.
char* __cdecl RecordMedian(char* a, char* b, char* c, std::uint32_t)
{
    static bool logged;
    FirstCall("RecordMedian", logged);
    if (Before(a, b)) {
        if (Before(b, c))
            return b;
        return Before(a, c) ? c : a;
    }
    if (Before(a, c))
        return a;
    return Before(b, c) ? c : b;
}

// 0x00522270 (cdecl: first, last, the pivot by value, extra): the
// unguarded partition; the swap through a record copy destroyed inline;
// the pivot destroyed. Answers the cut.
char* __cdecl RecordPartition(char* first, char* last, RecordBytes pivot, std::uint32_t)
{
    static bool logged;
    FirstCall("RecordPartition", logged);
    for (;;) {
        while (Before(first, pivot.bytes))
            first += kRecordSize;
        do
            last -= kRecordSize;
        while (Before(pivot.bytes, last));
        if (!(first < last))
            break;
        RecordBytes swap;
        CopyRecord(swap.bytes, first);
        AssignRecord(first, last);
        AssignRecord(last, swap.bytes);
        DestroyRecordInline(reinterpret_cast<char*>(swap.bytes));
        first += kRecordSize;
    }
    DestroyRecord(pivot.bytes);
    return first;
}

// 0x005215A0 (cdecl: base, hole, top, the value by value, extra):
// push_heap; the value destroyed.
void __cdecl RecordPushHeap(char* base, std::int32_t hole, std::int32_t top, RecordBytes value, std::uint32_t)
{
    static bool logged;
    FirstCall("RecordPushHeap", logged);
    std::int32_t parent = (hole - 1) / 2;
    while (hole > top) {
        char* const p = base + parent * static_cast<std::int32_t>(kRecordSize);
        if (!Before(p, value.bytes))
            break;
        AssignRecord(base + hole * static_cast<std::int32_t>(kRecordSize), p);
        hole = parent;
        parent = (parent - 1) / 2;
    }
    AssignRecord(base + hole * static_cast<std::int32_t>(kRecordSize), value.bytes);
    DestroyRecord(value.bytes);
}

// 0x00521BB0 (cdecl: base, hole, length, the value by value, extra):
// adjust_heap down to a leaf, then push_heap with a copy of the value; the
// value destroyed.
void __cdecl RecordAdjustHeap(char* base, std::int32_t hole, std::int32_t length, RecordBytes value,
                              std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordAdjustHeap", logged);
    const std::int32_t size = static_cast<std::int32_t>(kRecordSize);
    const std::int32_t top = hole;
    std::int32_t child = 2 * hole + 2;
    while (child < length) {
        if (Before(base + child * size, base + (child - 1) * size))
            --child;
        AssignRecord(base + hole * size, base + child * size);
        hole = child;
        child = 2 * child + 2;
    }
    if (child == length) {
        AssignRecord(base + hole * size, base + (child - 1) * size);
        hole = child - 1;
    }
    reinterpret_cast<ByValueHeap>(addr::SC4BehaviourRecord_PushHeap)(base, hole, top, CopyOf(value.bytes), extra);
    DestroyRecord(value.bytes);
}

// 0x00522350 (cdecl: first, last, extra, two unused): make_heap.
void __cdecl RecordMakeHeap(char* first, char* last, std::uint32_t extra, std::uint32_t, std::uint32_t)
{
    static bool logged;
    FirstCall("RecordMakeHeap", logged);
    const std::int32_t length = RecordCount(first, last);
    if (length < 2)
        return;
    std::int32_t parent = (length - 2) / 2;
    char* at = first + parent * static_cast<std::int32_t>(kRecordSize);
    for (;;) {
        reinterpret_cast<ByValueHeap>(addr::SC4BehaviourRecord_AdjustHeap)(first, parent, length, CopyOf(at), extra);
        if (parent == 0)
            return;
        --parent;
        at -= kRecordSize;
    }
}

// 0x00522450 (cdecl: first, last, extra): sort_heap.
void __cdecl RecordSortHeap(char* first, char* last, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordSortHeap", logged);
    std::int32_t bytes = static_cast<std::int32_t>(last - first);
    while (bytes / static_cast<std::int32_t>(kRecordSize) > 1) {
        char* const end = first + bytes - kRecordSize;
        RecordBytes held;
        CopyRecord(held.bytes, end);
        AssignRecord(end, first);
        const std::int32_t remaining = bytes - static_cast<std::int32_t>(kRecordSize);
        reinterpret_cast<ByValueHeap>(addr::SC4BehaviourRecord_AdjustHeap)(
            first, 0, remaining / static_cast<std::int32_t>(kRecordSize), CopyOf(held.bytes), extra);
        DestroyRecordInline(reinterpret_cast<char*>(held.bytes));
        bytes = remaining;
    }
}

// 0x00522590 (cdecl: first, middle, last, unused, extra): partial_sort
// (make_heap of [first, middle), each later record before the top swapped
// in, sort_heap).
void __cdecl RecordPartialSort(char* first, char* middle, char* last, std::uint32_t, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordPartialSort", logged);
    CallCdecl<void>(addr::SC4BehaviourRecord_MakeHeap, first, middle, extra, 0u, 0u);
    for (char* i = middle; i < last; i += kRecordSize) {
        if (!Before(i, first))
            continue;
        RecordBytes held;
        CopyRecord(held.bytes, i);
        AssignRecord(i, first);
        reinterpret_cast<ByValueHeap>(addr::SC4BehaviourRecord_AdjustHeap)(first, 0, RecordCount(first, middle),
                                                                            CopyOf(held.bytes), extra);
        DestroyRecordInline(reinterpret_cast<char*>(held.bytes));
    }
    CallCdecl<void>(addr::SC4BehaviourRecord_SortHeap, first, middle, extra);
}

// 0x00522720 (cdecl: first, last, unused, depth, extra): the introsort
// loop above 16 records: partial_sort at depth 0; else the median of the
// first, middle and last-but-one, the partition around a copy of it, the
// loop on the right part and again on the left.
void __cdecl RecordIntrosortLoop(char* first, char* last, std::uint32_t, std::int32_t depth, std::uint32_t extra)
{
    static bool logged;
    FirstCall("RecordIntrosortLoop", logged);
    while (RecordCount(first, last) > 16) {
        if (depth == 0) {
            CallCdecl<void>(addr::SC4BehaviourRecord_PartialSort, first, last, last, 0u, extra);
            return;
        }
        --depth;
        char* const middle = first + (RecordCount(first, last) / 2) * static_cast<std::int32_t>(kRecordSize);
        char* const pivot =
            CallCdecl<char*>(addr::SC4BehaviourRecord_Median, first, middle, last - kRecordSize, extra);
        char* const cut =
            reinterpret_cast<ByValuePartition>(addr::SC4BehaviourRecord_Partition)(first, last, CopyOf(pivot), extra);
        CallCdecl<void>(addr::SC4BehaviourRecord_IntrosortLoop, cut, last, 0u, depth, extra);
        last = cut;
    }
}

// 0x00522800 (cdecl: first, last, the empty predicate): std::sort: the
// introsort loop with depth twice the floor of log2 of the count, then the
// final insertion sort. Its only caller pushes two arguments; the game
// forwards the word of its first argument's slot as the predicate the
// callees ignore, and so does ours.
void __cdecl RecordSort(char* first, char* last, std::uint32_t)
{
    static bool logged;
    FirstCall("RecordSort", logged);
    if (first == last)
        return;
    std::int32_t n = RecordCount(first, last);
    std::int32_t k = 0;
    while (n != 1) {
        n >>= 1;
        ++k;
    }
    const std::uint32_t predicate = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(first));
    CallCdecl<void>(addr::SC4BehaviourRecord_IntrosortLoop, first, last, 0u, k + k, predicate);
    CallCdecl<void>(addr::SC4BehaviourRecord_FinalInsertionSort, first, last, predicate);
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

const Detour kTemplates[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataControllerTemplate_Construct, TemplateConstruct),
    OPENSC4_DETOUR(addr::cSC4AttractorTemplate_Construct, AttractorTemplateConstruct),
    OPENSC4_DETOUR(addr::cSC4GeneratorTemplate_Construct, GeneratorTemplateConstruct),
    OPENSC4_DETOUR(addr::cSC4AttractorTemplate_DeletingDestructor, AttractorTemplateDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AttractorTemplate_Destroy, AttractorTemplateDestroy),
    OPENSC4_DETOUR(addr::cSC4GeneratorTemplate_DeletingDestructor, GeneratorTemplateDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4GeneratorTemplate_Destroy, GeneratorTemplateDestroy),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_TemplateFactory, TemplateFactory),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_CreateControllerTemplate, CreateControllerTemplate),
};

const Detour kLua[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaCreateAutomaton, LuaCreateAutomaton),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaCreateController, LuaCreateController),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaAttachController, LuaAttachController),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaAttachControllerGroup, LuaAttachControllerGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaDetachController, LuaDetachController),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaDetachControllerGroup, LuaDetachControllerGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaGetSourceBuildingCount, LuaGetSourceBuildingCount),
};

const Detour kHelpers[] = {
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_CacheIds, GroupCacheIds),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Record3Construct, Record3Construct),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Record3Destroy, Record3Destroy),
    OPENSC4_DETOUR(addr::cSC4AutomataScriptSystem_LuaToId, LuaToId),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_FindRecord, GroupFindRecord),
    OPENSC4_DETOUR(addr::cSC4OccupantGroupTemplate_Record3Assign, Record3Assign),
};

const Detour kRecords[] = {
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_VectorInsertOverflow, RecordVectorInsertOverflow),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_Assign, RecordAssign),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_UnguardedLinearInsert, RecordUnguardedLinearInsert),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_PushHeap, RecordPushHeap),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_CopyBackward, RecordCopyBackward),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_CopyForward, RecordCopyForward),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_UnguardedInsertionSort, RecordUnguardedInsertionSort),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_AdjustHeap, RecordAdjustHeap),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_Partition, RecordPartition),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_MakeHeap, RecordMakeHeap),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_LinearInsert, RecordLinearInsert),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_SortHeap, RecordSortHeap),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_InsertionSort, RecordInsertionSort),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_PartialSort, RecordPartialSort),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_FinalInsertionSort, RecordFinalInsertionSort),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_IntrosortLoop, RecordIntrosortLoop),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_Sort, RecordSort),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_Copy, RecordCopy),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_UninitializedCopy, RecordUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_Median, RecordMedian),
    OPENSC4_DETOUR(addr::SC4BehaviourRecord_Construct, RecordConstruct),
};

#undef OPENSC4_DETOUR

bool InstallTemplates(registry::Mode)
{
    return InstallList(kTemplates, sizeof kTemplates / sizeof kTemplates[0], "templates");
}
bool InstallLua(registry::Mode) { return InstallList(kLua, sizeof kLua / sizeof kLua[0], "lua"); }
bool InstallHelpers(registry::Mode) { return InstallList(kHelpers, sizeof kHelpers / sizeof kHelpers[0], "helpers"); }
bool InstallRecords(registry::Mode) { return InstallList(kRecords, sizeof kRecords / sizeof kRecords[0], "records"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.scripts.templates", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_CreateControllerTemplate,
                        "Our automata script system's controller templates: the base, attractor and generator "
                        "template constructors, destructor bodies and deleting destructors, the factory by class "
                        "and CreateControllerTemplate (slot 16) (9 detours; part s2, "
                        "src/core/automata_scripts_2.cpp)",
                        false, InstallTemplates}),
    registry::Register({"simulators.automata.scripts.lua", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataScriptSystem_LuaCreateAutomaton,
                        "Our automata script system's Lua functions: create_automaton, create_controller, "
                        "attach_controller, attach_controller_group, detach_controller, detach_controller_group "
                        "and get_source_building_count (7 detours; part s2, src/core/automata_scripts_2.cpp)",
                        false, InstallLua}),
    registry::Register({"simulators.automata.scripts.helpers", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4OccupantGroupTemplate_FindRecord,
                        "Our automata script system's helpers for the managers and the U-Drive: a group's cached "
                        "ID list and its record lookup, the three-string record's constructor, destructor and "
                        "assignment, and the Lua value to ID (6 detours; part s2, src/core/automata_scripts_2.cpp)",
                        false, InstallHelpers}),
    registry::Register({"simulators.automata.scripts.records", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4BehaviourRecord_Sort,
                        "Our automata script system's behaviour records: the record's constructor, copy and "
                        "assignment, the vector insert, and the std::sort over them (insertion sorts, the "
                        "introsort loop, median, partition, heap fallback) (21 detours; part s2, "
                        "src/core/automata_scripts_2.cpp)",
                        false, InstallRecords}),
};

} // namespace
} // namespace opensc4::automata_scripts
