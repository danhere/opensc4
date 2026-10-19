// Our vehicle and pedestrian managers, part v1 (build 641): the object (the
// two factories, the three constructors, the destructor body, the deleting
// destructor and the record list's clear; QueryInterface and the
// serializable part's GetGZCLSID of both classes) and the queries (the
// kind tables of slots 73, 74, 85 and 86, slot 87's scale, slot 88's
// matrix, the automaton's class (68), the rate (75), the bounds test (41),
// the neighbour cell test (82), the record list's hit test and the group
// tests). The class map is in src/core/automata_vehmgr.h and the survey in
// docs/automata.md, "Part v1". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Group, off by default:
//   simulators.automata.vehmgr: the 32 detours listed in kObject below.
//
// What it does. The two managers keep the city's road vehicles and
// pedestrians (the automata of classes 0x896E714A and 0x896E75AF). Each
// constructor runs the shared base's (0x00554360, which runs the common
// base's 0x005125E0), sets its four vtables, its occupant group, its
// automaton class and its default class, and appends its kinds to the kind
// list (the vehicle manager 1, 2, 3, 5, 4, 7, 6, 8, 9, 10; the pedestrian
// manager 1, 7, 8). The factories answer the object + 8, the serializable
// part. QueryInterface answers the class's own ID with the object and
// leaves the rest to the common base's 0x00505480. The kinds name curves
// (static SC4Curve records at 0x00B21BF4..0x00B21C70, slot 85) and their
// floats (slot 86), a class (73) and an occupant group (74).
//
// Oddities kept: QueryInterface's own branch writes the object and calls
// AddRef through it without a test (the address is held in a volatile
// local, as in parts a1 and a3a); the constructors test the pair inside a
// new list node for null (node + 8) before they write it; the destructor
// body frees the record vector's buffer and the record list's head but
// destroys neither the records nor the vector; IsAutomatonOccupant asks
// the automaton for its occupant and type twice; TestNeighbourCell ignores
// its first argument and answers true for an empty cell; ScaledMatrix
// transposes the matrix when the scale is negative (or NaN). The game's
// stack leftovers are never read, except one: KindRate reads the float the
// 24-hour clock's slot 0x50 writes into a local; ours starts that local at
// 0 (the slot always writes it).
//
// Floats: 53-bit doubles, as the game's x87 runs. Values the game answers
// in ST0 unrounded (GetScale, KindRate, the curve, the +0xA84 object's
// slot 0x5C) are doubles; floats are stored where the game stores them
// (ScaledMatrix's scale, InExtendedBounds's rectangle, the int to float of
// KindRate). Compares keep the asm's handling of unordered values.
//
// Still the game's, at their addresses: the common base (its constructor
// 0x005125E0, destructor 0x00512780, QueryInterface 0x00505480, AddRef
// 0x00505510 and Release 0x00507B50, slot 1 and 2 of both managers); the
// adjustor thunks 0x0055FE30, 0x0055FE40, 0x0055FE50, 0x00545640,
// 0x00545650 and 0x00545660 (`sub ecx,N; jmp`, which reach our
// QueryInterface); the plain jumps 0x0054D250 and 0x0054D780; SC4Stopwatch,
// SC4PairList::Destroy, SC4Curve::Evaluate and the allocators. Parts v2 and
// v3 (Init, Shutdown, the messages, the save, the automata's creation,
// dispatch and frames) are the game's.
#include "core/automata_vehmgr.h"

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

namespace opensc4::automata_vehmgr {
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
    log::Write("automata vehmgr: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

// The derived constructors' push_back of a (kind, 0) pair on the kind list
// (+0xA94), inline in the game.
void AppendKind(char* self, std::uint32_t kind)
{
    char* const head = At<char*>(self, field::kKinds);
    char* const node = static_cast<char*>(Allocate(0x10));
    if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0) {
        At<std::uint32_t>(node, 8) = kind;
        At<std::uint32_t>(node, 0xC) = 0;
    }
    char* const last = At<char*>(head, 4);
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
}

// ====================================================================================
// The object

// 0x00554360, the shared base's constructor both derived constructors call.
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("base Construct", logged);
    CallThis<char*>(addr::cSC4AutomataManager_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4VehPedManagerBase_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4VehPedManagerBase_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4VehPedManagerBase_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4VehPedManagerBase_MessageVtable;
    At<std::uint32_t>(self, field::kKinds) = 0;
    char* const kinds = static_cast<char*>(Allocate(0x10));
    At<char*>(kinds, 0) = kinds;
    At<char*>(kinds, 4) = kinds;
    At<char*>(self, field::kKinds) = kinds;
    At<std::uint32_t>(self, field::kA9C) = 0xFFFFFFFFu;
    At<std::uint32_t>(self, field::kAA0) = 0xFFFFFFFFu;
    At<std::uint32_t>(self, field::kAA4) = 0;
    At<std::uint32_t>(self, field::kRecordsBegin) = 0;
    At<std::uint32_t>(self, field::kRecordsEnd) = 0;
    At<std::uint32_t>(self, field::kRecordsCapacity) = 0;
    At<std::uint32_t>(self, field::kRecordList) = 0;
    char* const records = static_cast<char*>(Allocate(0x40));
    At<char*>(records, 0) = records;
    At<char*>(records, 4) = records;
    At<char*>(self, field::kRecordList) = records;
    CallThis<void*>(addr::SC4Stopwatch_Construct, self + field::kStopwatch, static_cast<std::uint32_t>(4));
    At<std::uint32_t>(self, field::kAD8) = 0;
    At<std::uint32_t>(self, field::kADC) = 0;
    return self;
}

// 0x0055FB50, the vehicle manager's constructor.
char* __fastcall VehicleConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("vehicle Construct", logged);
    CallThis<char*>(addr::cSC4VehPedManagerBase_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4VehicleManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4VehicleManager_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4VehicleManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4VehicleManager_MessageVtable;
    At<std::uint32_t>(self, field::kVehicleAE0) = 0;
    At<std::uint32_t>(self, field::kA40) = 0x16;
    At<std::uint32_t>(self, field::kGroup) = 0x4002;
    At<std::uint32_t>(self, field::kAutomatonClass) = kVehicleAutomatonClass;
    At<std::uint32_t>(self, field::kDefaultClass) = 1;
    At<std::uint32_t>(self, field::kA90) = 1;
    static constexpr std::uint32_t kKinds[] = {1, 2, 3, 5, 4, 7, 6, 8, 9, 10};
    for (const std::uint32_t kind : kKinds)
        AppendKind(self, kind);
    At<char*>(self, field::kFirstKind) = At<char*>(At<char*>(self, field::kKinds), 0);
    At<std::uint32_t>(self, field::kAA4) = 0x3E4CCCCDu; // 0.2f
    return self;
}

// 0x00545500, the pedestrian manager's constructor.
char* __fastcall PedestrianConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("pedestrian Construct", logged);
    CallThis<char*>(addr::cSC4VehPedManagerBase_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4PedestrianManager_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4PedestrianManager_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4PedestrianManager_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4PedestrianManager_MessageVtable;
    At<std::uint32_t>(self, field::kA40) = 0x17;
    At<std::uint32_t>(self, field::kGroup) = 0x4001;
    At<std::uint32_t>(self, field::kAutomatonClass) = kPedestrianAutomatonClass;
    At<std::uint32_t>(self, field::kDefaultClass) = 2;
    At<std::uint32_t>(self, field::kA90) = 0;
    static constexpr std::uint32_t kKinds[] = {1, 7, 8};
    for (const std::uint32_t kind : kKinds)
        AppendKind(self, kind);
    At<char*>(self, field::kFirstKind) = At<char*>(At<char*>(self, field::kKinds), 0);
    At<std::uint32_t>(self, field::kAA4) = 0x3F800000u; // 1.0f
    return self;
}

// 0x005145A0: 0xAE8 bytes from SC4::operator_new, the constructor, the
// object + 8 (the serializable part).
void* __cdecl VehicleCreate()
{
    static bool logged;
    FirstCall("vehicle Create", logged);
    char* const self = static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(kVehicleSize)));
    if (!self)
        return nullptr;
    char* const made = CallThis<char*>(addr::cSC4VehicleManager_Construct, self);
    if (!made)
        return nullptr;
    return made + field::kSerializable;
}

// 0x005145D0: 0xAE0 bytes, the constructor, the object + 8.
void* __cdecl PedestrianCreate()
{
    static bool logged;
    FirstCall("pedestrian Create", logged);
    char* const self =
        static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(kPedestrianSize)));
    if (!self)
        return nullptr;
    char* const made = CallThis<char*>(addr::cSC4PedestrianManager_Construct, self);
    if (!made)
        return nullptr;
    return made + field::kSerializable;
}

// 0x00551FC0, on the record list field (+0xAB8): each node's object
// released and the node freed, the head linked to itself; answers the head.
void* __fastcall ClearRecordList(char** list, void*)
{
    static bool logged;
    FirstCall("ClearRecordList", logged);
    char* node = At<char*>(*list, 0);
    while (node != *list) {
        char* const next = At<char*>(node, 0);
        if (void* const object = At<void*>(node, record::kObject))
            Virtual<std::uint32_t>(object, slot::kRelease);
        Free(node);
        node = next;
    }
    char* const head = *list;
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    return head;
}

// 0x00553860, the destructor body; the game tail-jumps to the common base's.
void* __fastcall Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Destroy", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4VehPedManagerBase_Vtable;
    At<std::uintptr_t>(self, field::kVtable4) = addr::cSC4VehPedManagerBase_Vtable4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4VehPedManagerBase_SerializableVtable;
    At<std::uintptr_t>(self, field::kMessage) = addr::cSC4VehPedManagerBase_MessageVtable;
    CallThis<void>(addr::SC4Stopwatch_Destruct, self + field::kStopwatch);
    CallThis<void*>(addr::cSC4VehPedManagerBase_ClearRecordList, self + field::kRecordList);
    if (void* const head = At<void*>(self, field::kRecordList))
        Free(head);
    if (void* const begin = At<void*>(self, field::kRecordsBegin))
        Free(begin);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kKinds);
    return CallThis<void*>(addr::cSC4AutomataManager_Destroy, self);
}

// 0x00545670, slot 38 of both managers (flags), ret 4.
char* __fastcall DeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    CallThis<void*>(addr::cSC4VehPedManagerBase_Destroy, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

std::uint8_t QueryInterfaceOf(char* self, std::uint32_t own, std::uint32_t iid, void** out)
{
    char* volatile object = self; // the game calls through it without a test
    if (iid == own) {
        *out = object;
        Virtual<std::uint32_t>(object, slot::kAddRef);
        return 1;
    }
    return CallThis<std::uint8_t>(addr::cSC4AutomataManager_QueryInterface, self, iid, out);
}

// 0x0055B630, slot 0 (iid, out), ret 8.
std::uint8_t __fastcall VehicleQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("vehicle QueryInterface", logged);
    return QueryInterfaceOf(self, kVehicleClassId, iid, out);
}

// 0x005444C0, slot 0, ret 8.
std::uint8_t __fastcall PedestrianQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("pedestrian QueryInterface", logged);
    return QueryInterfaceOf(self, kPedestrianClassId, iid, out);
}

// 0x0055FE20 and 0x00545630, the serializable part's slot 5.
std::uint32_t __fastcall VehicleGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("vehicle GetGZCLSID", logged);
    return kVehicleClassId;
}

std::uint32_t __fastcall PedestrianGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("pedestrian GetGZCLSID", logged);
    return kPedestrianClassId;
}

// ====================================================================================
// The kind tables

// 0x0054D560, slot 85's base (kind), ret 4; this not read.
void* __fastcall GetKindCurve(char*, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("GetKindCurve", logged);
    switch (kind) {
    case 2:
        return reinterpret_cast<void*>(addr::SC4VehPedManager_CurveKind2);
    case 3:
        return reinterpret_cast<void*>(addr::SC4VehPedManager_CurveKind3);
    case 5:
        return reinterpret_cast<void*>(addr::SC4VehPedManager_CurveKind5);
    case 4:
    case 6:
    case 9:
    case 10:
        return reinterpret_cast<void*>(addr::SC4VehPedManager_CurveKind4);
    default:
        return reinterpret_cast<void*>(addr::SC4VehPedManager_CurveDefault);
    }
}

// The float at +0x10 of a curve record (slot 86).
float CurveValue(std::uintptr_t curve) { return Global<float>(curve + 0x10); }

// 0x0054D5C0, slot 86's base (kind), ret 4.
float __fastcall GetKindValue(char*, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("GetKindValue", logged);
    switch (kind) {
    case 2:
        return CurveValue(addr::SC4VehPedManager_CurveKind2);
    case 3:
        return CurveValue(addr::SC4VehPedManager_CurveKind3);
    case 5:
        return CurveValue(addr::SC4VehPedManager_CurveKind5);
    case 4:
    case 6:
    case 9:
    case 10:
        return CurveValue(addr::SC4VehPedManager_CurveKind4);
    default:
        return CurveValue(addr::SC4VehPedManager_CurveDefault);
    }
}

// 0x0055B440, the vehicle manager's slot 85: kinds 1 and 7 the default
// curve, else the base (the game tail-jumps).
void* __fastcall VehicleGetKindCurve(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("vehicle GetKindCurve", logged);
    if (kind == 1 || kind == 7)
        return reinterpret_cast<void*>(addr::SC4VehPedManager_CurveDefault);
    return CallThis<void*>(addr::cSC4VehPedManagerBase_GetKindCurve, self, kind);
}

// 0x0055B460, the vehicle manager's slot 86.
float __fastcall VehicleGetKindValue(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("vehicle GetKindValue", logged);
    if (kind == 1 || kind == 7)
        return CurveValue(addr::SC4VehPedManager_CurveDefault);
    return CallThis<float>(addr::cSC4VehPedManagerBase_GetKindValue, self, kind);
}

// 0x00544440, the pedestrian manager's slot 85: kinds 1 and 7 its own curve.
void* __fastcall PedestrianGetKindCurve(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("pedestrian GetKindCurve", logged);
    if (kind == 1 || kind == 7)
        return reinterpret_cast<void*>(addr::SC4PedestrianManager_CurveKind1);
    return CallThis<void*>(addr::cSC4VehPedManagerBase_GetKindCurve, self, kind);
}

// 0x00544460, the pedestrian manager's slot 86.
float __fastcall PedestrianGetKindValue(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("pedestrian GetKindValue", logged);
    if (kind == 1 || kind == 7)
        return CurveValue(addr::SC4PedestrianManager_CurveKind1);
    return CallThis<float>(addr::cSC4VehPedManagerBase_GetKindValue, self, kind);
}

// 0x0054D3C0, slot 73 (kind), ret 4.
std::uint32_t __fastcall KindClass(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("KindClass", logged);
    switch (kind) {
    case 4:
    case 5:
        return 3;
    case 6:
        return 4;
    case 9:
        return 6;
    case 10:
        return 7;
    default:
        return At<std::uint32_t>(self, field::kDefaultClass);
    }
}

// 0x0054D420, slot 74 (kind), ret 4: the kind's occupant group.
std::uint32_t __fastcall KindGroup(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("KindGroup", logged);
    switch (kind) {
    case 2:
        return 0x4200;
    case 3:
        return 0x4204;
    case 4:
        return 0x4215;
    case 5:
        return 0x4216;
    case 6:
        return 0x4217;
    case 8:
        return 0x4102;
    case 9:
        return 0x422C;
    case 10:
        return 0x422E;
    default:
        return At<std::uint32_t>(self, field::kGroup);
    }
}

// ====================================================================================
// The other queries

// 0x0054D630, slot 87 (a, b), ret 8; b not read. Unrounded in ST0.
double __fastcall GetScale(char* self, void*, std::uint32_t a, std::uint32_t)
{
    static bool logged;
    FirstCall("GetScale", logged);
    const double one = static_cast<double>(Global<float>(addr::SC4_FloatOneA81228));
    if (!At<std::uint8_t>(self, field::kScaleOn))
        return one;
    const double source = Virtual<double>(At<void*>(self, field::kScaleSource), 0x5C, a);
    const double scaled = source * static_cast<double>(Global<float>(addr::SC4Traffic_FerryCostScale)); // 0.01
    const double value = one - scaled;
    const double low = static_cast<double>(Global<float>(addr::SC4_FloatZero));
    if (value < low)
        return low;
    const double high = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    if (value > high)
        return high;
    return value; // and a NaN
}

// 0x00544480, the pedestrian manager's slot 87, ret 8.
double __fastcall PedestrianGetScale(char* self, void*, std::uint32_t a, std::uint32_t)
{
    static bool logged;
    FirstCall("pedestrian GetScale", logged);
    const double one = static_cast<double>(Global<float>(addr::SC4_FloatOneA81228));
    if (!At<std::uint8_t>(self, field::kScaleOn))
        return one;
    const double source = Virtual<double>(At<void*>(self, field::kScaleSource), 0x5C, a);
    const double scaled = source * static_cast<double>(Global<float>(addr::SC4_Float0_02));
    return scaled + one;
}

// 0x0054D6C0, slot 68 (automaton), ret 4.
std::uint32_t __fastcall AutomatonClass(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("AutomatonClass", logged);
    if (!Virtual<std::uint8_t>(automaton, 0x234))
        return At<std::uint32_t>(self, field::kDefaultClass);
    void* const groups = static_cast<char*>(automaton) + 4;
    auto in = [groups](std::uint32_t group) { return Virtual<std::uint8_t>(groups, 0x58, group) != 0; };
    if (in(0x422F) || in(0x422E))
        return 7;
    if (in(0x422D) || in(0x422C))
        return 6;
    if (in(0x4208) || in(0x4217))
        return 4;
    return 3;
}

// 0x0054D150 (automaton), ret 4; this not read.
std::uint32_t __fastcall IsAutomatonOccupant(void*, void*, void* automaton)
{
    static bool logged;
    FirstCall("IsAutomatonOccupant", logged);
    if (!automaton)
        return 0;
    if (Virtual<std::uint32_t>(Virtual<void*>(automaton, 0x14), 0x1C) == 0xC7CC75F1u)
        return 1;
    if (Virtual<std::uint32_t>(Virtual<void*>(automaton, 0x14), 0x1C) == 0x496E5783u)
        return 1;
    return 0;
}

// 0x0054D1A0 (group), ret 4.
std::uint32_t __fastcall IsServiceGroup(void*, void*, std::uint32_t group)
{
    static bool logged;
    FirstCall("IsServiceGroup", logged);
    switch (group) {
    case 0x4005:
    case 0x4201:
    case 0x4203:
    case 0x4215:
    case 0x4216:
    case 0x4208:
    case 0x4217:
    case 0x422D:
    case 0x422C:
    case 0x422F:
    case 0x422E:
    case 0x4230:
    case 0x4233:
        return 1;
    default:
        return 0;
    }
}

// 0x0054D210 (group), ret 4.
std::uint32_t __fastcall IsGroup422C(void*, void*, std::uint32_t group)
{
    static bool logged;
    FirstCall("IsGroup422C", logged);
    return (group == 0x422D || group == 0x422C) ? 1u : 0u;
}

// 0x0054D230 (group), ret 4.
std::uint32_t __fastcall IsGroup422E(void*, void*, std::uint32_t group)
{
    static bool logged;
    FirstCall("IsGroup422E", logged);
    return (group == 0x422F || group == 0x422E) ? 1u : 0u;
}

// 0x0054DF40, slot 75 (kind), ret 4. Unrounded in ST0.
double __fastcall KindRate(char* self, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("KindRate", logged);
    void* const dates = Virtual<void*>(Global<void*>(addr::SC4_City), 0x110);
    const std::int32_t count = Virtual<std::int32_t>(dates, 0x40);
    void* const clock = Virtual<void*>(Global<void*>(addr::SC4_City), 0x158); // Get24HourClock
    float hour = 0.0f;
    Virtual<std::uint32_t>(clock, 0x50, &hour);
    void* const curve = Virtual<void*>(self, slot::kGetKindCurve, kind);
    const double value = CallThis<double>(addr::SC4Curve_Evaluate, curve, static_cast<float>(count));
    const double scaled = static_cast<double>(hour) * static_cast<double>(Global<float>(addr::SC4VehPedManager_RateScale));
    return value * scaled;
}

// 0x0054DFB0, slot 41 (object), ret 4. The game's frame: the object's
// position (slot 0x1C writes x, y, z) right below the grown rectangle.
std::uint8_t __fastcall InExtendedBounds(char* self, void*, void* object)
{
    static bool logged;
    FirstCall("InExtendedBounds", logged);
    if (!object)
        return 0;
    struct {
        float position[3];
        float box[4];
    } frame{};
    const double margin = static_cast<double>(Global<float>(addr::SC4VehPedManager_BoundsMargin));
    const float* const rect = &At<float>(self, field::kRect);
    frame.box[0] = static_cast<float>(static_cast<double>(rect[0]) - margin);
    frame.box[1] = static_cast<float>(static_cast<double>(rect[1]) - margin);
    frame.box[2] = static_cast<float>(static_cast<double>(rect[2]) + margin);
    frame.box[3] = static_cast<float>(static_cast<double>(rect[3]) + margin);
    Virtual<std::uint32_t>(object, 0x1C, frame.position);
    const float x = frame.position[0], z = frame.position[2];
    return (x >= frame.box[0] && z >= frame.box[1] && x <= frame.box[2] && z <= frame.box[3]) ? 1 : 0;
}

// 0x0054D260, slot 82 (a, x, z, direction), ret 0x10; a not read.
std::uint8_t __fastcall TestNeighbourCell(char* self, void*, std::uint32_t, std::int32_t x, std::int32_t z,
                                          std::uint32_t direction)
{
    static bool logged;
    FirstCall("TestNeighbourCell", logged);
    const std::int32_t* const rect = Virtual<const std::int32_t*>(At<void*>(self, field::kCells), 0x44);
    switch (direction & 0xFF) {
    case 0:
        --x;
        break;
    case 1:
        --z;
        break;
    case 2:
        ++x;
        break;
    case 3:
        ++z;
        break;
    default:
        break;
    }
    if (x < rect[0] || z < rect[1] || x > rect[2] || z > rect[3])
        return 1;
    void* const cell = Virtual<void*>(At<void*>(self, field::kCells), 0x2C, x, z);
    if (!cell)
        return 1;
    if (Virtual<std::uint8_t>(cell, 0x20, 0x200000u))
        return 1;
    return 0;
}

// 0x0054D4A0, slot 88 (a, data, scale, out), ret 0x10.
void __fastcall ScaledMatrix(char* self, void*, std::uint32_t a, const char* data, float scale, float* out)
{
    static bool logged;
    FirstCall("ScaledMatrix", logged);
    const float factor = static_cast<float>(Virtual<double>(self, slot::kGetScale, a, static_cast<std::uint32_t>(7)));
    const std::uint16_t* const words = reinterpret_cast<const std::uint16_t*>(data + 0x34);
    if (scale >= Global<float>(addr::SC4_FloatZero)) {
        for (int i = 0; i < 16; ++i)
            out[i] = static_cast<float>(static_cast<double>(words[i]) * static_cast<double>(factor) *
                                        static_cast<double>(scale));
        return;
    }
    const double negated = -static_cast<double>(scale);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            out[row * 4 + column] = static_cast<float>(
                static_cast<double>(words[column * 4 + row]) * static_cast<double>(factor) * negated);
}

// 0x00551630, on the record list field (+0xAB8): (x, z), ret 8.
std::uint32_t __fastcall RecordListHits(char** list, void*, std::int32_t x, std::int32_t z)
{
    static bool logged;
    FirstCall("RecordListHits", logged);
    std::uint32_t hits = 0;
    char* const head = *list;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        const std::int32_t* const a = &At<std::int32_t>(node, record::kRect1);
        if (x >= a[0] && z >= a[1] && x <= a[2] && z <= a[3])
            hits |= 1;
        const std::int32_t* const b = &At<std::int32_t>(node, record::kRect2);
        if (x >= b[0] && z >= b[1] && x <= b[2] && z <= b[3])
            hits |= 2;
    }
    return hits;
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
            log::Write("automata vehmgr %s: no address entry for 0x%08X", what,
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
    log::Write("automata vehmgr %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4VehicleManager_Create, VehicleCreate),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_Create, PedestrianCreate),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_Construct, VehicleConstruct),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_Construct, PedestrianConstruct),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_ClearRecordList, ClearRecordList),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_QueryInterface, VehicleQueryInterface),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_QueryInterface, PedestrianQueryInterface),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_GetGZCLSID, VehicleGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_GetGZCLSID, PedestrianGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_GetKindCurve, GetKindCurve),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_GetKindValue, GetKindValue),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_GetKindCurve, VehicleGetKindCurve),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_GetKindValue, VehicleGetKindValue),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_GetKindCurve, PedestrianGetKindCurve),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_GetKindValue, PedestrianGetKindValue),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_GetScale, GetScale),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_GetScale, PedestrianGetScale),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_KindClass, KindClass),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_KindGroup, KindGroup),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_AutomatonClass, AutomatonClass),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_IsAutomatonOccupant, IsAutomatonOccupant),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_IsServiceGroup, IsServiceGroup),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_IsGroup422C, IsGroup422C),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_IsGroup422E, IsGroup422E),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_KindRate, KindRate),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_InExtendedBounds, InExtendedBounds),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_TestNeighbourCell, TestNeighbourCell),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_ScaledMatrix, ScaledMatrix),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_RecordListHits, RecordListHits),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.vehmgr", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehPedManagerBase_Construct,
                        "Our vehicle and pedestrian managers, the object and its queries: the two factories, the "
                        "constructors (base, vehicle, pedestrian), the destructor body, the deleting destructor and "
                        "the record list's clear; QueryInterface and GetGZCLSID of both; the kind tables (slots 73, "
                        "74, 85, 86), the scale (87) and its matrix (88), the automaton's class (68), the rate (75), "
                        "the bounds test (41), the neighbour cell test (82), the record list's hit test and the "
                        "group tests of cSC4VehicleManager and cSC4PedestrianManager (32 detours; part v1, "
                        "src/core/automata_vehmgr.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_vehmgr
