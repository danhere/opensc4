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
// Groups, off by default:
//   simulators.automata.vehmgr: the 32 detours listed in kObject below.
//   simulators.automata.vehmgr.life, .messages, .serial and .add: part v2's
//   25 (life cycle, tuning, messages, the save, adding and removing), in
//   kLife, kMessages, kSerial and kAdd; described at "Part v2" below.
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
// SC4PairList::Destroy, SC4Curve::Evaluate and the allocators. Part v3 (the
// automata's creation, dispatch and frames) is the game's.
#include "core/automata_vehmgr.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
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
// Part v2: life cycle, tuning, messages, the save, adding and removing
//
// Four more groups, off by default, so part v1's group stays as it was:
//   simulators.automata.vehmgr.life (kLife, 13): Init (base 0x0054DBA0,
//   vehicle 0x0055B7E0, pedestrian 0x00544820), Shutdown (0x00552D00,
//   0x0055B880, 0x00544860) with the record list's release 0x00552A20 and the
//   overlay's stop 0x00562D60, HandleTuningParametersChanged (slot 24;
//   0x0055B410, 0x00544410), the tuning set-up (slot 58; 0x0055C9D0,
//   0x005450A0) and the vehicle manager's +4 slot 3 0x0055B3F0.
//   simulators.automata.vehmgr.messages (kMessages, 5): the message targets
//   (+0xC slot 3; 0x00562620, 0x00545450), the vehicle manager's controller
//   update 0x00561500, the pedestrian manager's 0x00544F30 and the
//   pedestrian's 0x005448D0 it calls.
//   simulators.automata.vehmgr.serial (kSerial, 4): the serializable part's
//   Write 0x00553570 and Read 0x00554410 with the record list's 0x00552A80
//   and 0x00553E90.
//   simulators.automata.vehmgr.add (kAdd, 3): AddAutomaton (slot 7;
//   0x00562C10, 0x00546990) and RemoveAutomaton (slot 8; 0x0054D690).
//
// What it does. Init, once (+0x34): the common base's Init (0x005066B0, which
// sets +0x34), +0xA9C and +0xAA0 back to -1, city slot 0x114's object kept
// at +0xA88 and referenced, the stopwatch started; the derived Inits add the
// message target (+0xC) to the message server for their messages (the
// vehicle manager 0xC53D10AA, 0xA6B79601, 0xA6B79611, 0xC9DA96E8,
// 0xC9DA96E9, 0x44778234, 0x0772FAD4; the pedestrian manager 0xA9E3B6E2)
// and run slots 24 and 58. Shutdown undoes it: +0xA88 released, each record
// of the +0xAA8 vector's object (+4) released and the vector emptied, the
// record list's objects given back (0x00552A20: to the controller manager's
// slot 0x38 when +0x38 is set, then released) and the list cleared
// (0x00551FC0), then the common base's Shutdown (0x005110A0) whatever +0x34
// says. The vehicle manager first unprepares and releases a static route
// line (0x00B22970, also taken off the view 0x00B43DD0) and stops and
// releases its +0xAE0 object. Slot 24 sets the most automata (slot 23) from
// the tuning's word (0x00B21BC8, 0x00B21BDC) and +0xA3C from its byte; slot
// 58 sets the most automata again from that word times a float of the app
// object 0x00B43CC4's +0x24 (+0x1FC, +0x224), rounded the game's way (plus
// 1.5 x 2^23, the float's bits less 0x4B400000), +0xA3C from a byte of its
// +0x18 (+0x224, +0x24C) less one, and calls slot 49 with the camera view's
// zoom and -1.
// The vehicle manager's messages: 0xA6B79601/0xA6B79611 stop the camera's
// follow (slot 34 with 0) when the followed automaton (0x00B21B8C) answers 0
// to its slot 0xE4 (0x10) and the follow is not locked (0x00B21B94);
// 0x0772FAD4 updates the controllers (below); 0x44778234 tells every
// automaton (slot 0x2C0) (1, 0) or (0, 0) when the message's slot 0x38 and
// the clock 0x00B43DBC's slot 0x18 agree (0 and 2, 1 and 1); 0xC9DA96E8 and
// 0xC9DA96E9 tell them (0, 1) and (1, 1); 0xC53D10AA with 0xA998D30B (its
// slot 0x24) reloads the tuning (0x00512CF0), runs slot 24 of the four
// automata managers (city slots 0xAC to 0xB8), the city situation
// manager's slot 0x7C (city slot 0xC4) and the cells object's slot 0x14 (+0xA7C).
// Every message then goes to the common base's DoMessage 0x00506970, whose
// answer is the answer. The controller update: for each occupant of city
// slot 0x110's two lists (slots 0xC4 and 0xBC), a flag from a property
// (0xEA56549E, 0x4A5654BA) of its slot 0x18 part in city slot 0xD0's tables
// (0xAA538CB3, 0x4A538CC6): set, and its value above 0x00B22974; then SetActive (slot
// 0x48) with that flag on the occupant's controller of one template (the
// script system's slot 0x44 by the name at 0x00B0A0D0) and on its
// controllers of four names (0x00B0A0BC; the controller manager's slots 0x48
// and 0x4C). The pedestrian manager's 0xA9E3B6E2 (0x230E27AC is answered 0
// at once): for the message's occupant (slot 0x80, asked for 0x26BB767E),
// each pedestrian of the list closer than the message's slot 0x38 (a
// distance, unsigned) and not in group 0x4000 is sent there (0x005448D0:
// +0x254 1 or 2 by the message's slot 0x24, slot 0x90 (+0x80, 1.0), slot
// 0x40 to the offset in x and z), and a message 0xA9E3B6E3 with the
// occupant, that value and the count goes to the message server.
// The save: Write puts version 3, the common base's Write (0x0050CB80),
// +0xA8C, the current kind (0 for none) and the record list: version 1, the
// count, per node +8, +0xC, +0x10 and the two rectangles, the object as a
// reference, +0x38 as a bool and +0x3C. Read takes versions 2 and 3, the
// common base's Read (0x005108E0), drops the saved +0xA8C, sets the current
// kind to the node of the saved kind (the first node for 0, the head when
// not found), and with
// version 3 reads the record list, appending a node per record (copied by
// 0x0054ED10, which references the object). Write answers 1 when the stream
// reports no error, Read the record list's answer or the base's.
// AddAutomaton with none makes one when the count (+0x48) is below the most
// (+0x3C): a block from the class's pool (0x00B0A0F8, 0x00B0A018; 0x0090CF9A
// grows it), built by 0x005628F0 / 0x00545F20 (the manager and three zero
// words); then the common base's AddAutomaton (0x0050F790). RemoveAutomaton
// is the common base's (0x0050C990) and slot 69.
//
// Oddities kept: Read drops the +0xA8C it reads, and turns a saved kind of 0
// (Write's for the head) into the first kind's node; the record list's read
// zeroes a record's object and then tests it for a release; the base
// Shutdown's empty erase (the vector's records after its end, none, moved
// down); the common base's Shutdown runs even when +0x34 is clear;
// 0x00561500 asks the occupant for its property part twice and fetches the node
// allocator for every list; the pedestrian message tests its occupant (still
// null) for a release before asking for it, and asks the message's type
// twice. The versions are read into the stack slot of the stream argument,
// as in part a3c, so a failed read compares the low word of the stream's
// address; ours starts the slot the same way. EAX's top bytes follow the
// game's wherever they come from a call (RemoveAutomaton, the full
// AddAutomaton, Read's version tests, the pedestrian messages, 0x005448D0,
// the +4 slot); the Inits, Shutdowns, tuning functions, the controller
// update and the gathering answer 1 where the game leaves call leftovers
// above AL (their callers read AL or replace them). Stack
// leftovers the game would read: Read's saved kind and the record list's
// count when the stream fails (ours start at 0), and the top three bytes of
// 0x00561500's flag, which goes to SetActive as a dword (both SetActive
// functions and what they pass it to read its low byte; ours sends 0 or 1).
//
// Floats as in part v1: 53-bit doubles. The tuning's word goes to a double
// unsigned (FILD plus 2^32 when negative); the product and the sum with
// 1.5 x 2^23 are rounded to doubles, then the sum to a float. The pedestrian distance is
// the square root of the sum (z, y, then x, of the squared differences) in
// doubles, compared with the value as a float, a NaN not closer; the
// differences are stored as floats where the game stores them. The
// controller update's property value is compared above the float threshold,
// a NaN not above.
//
// Calls: the message server's slots 0x10 (message, 0), 0x14 and 0x18
// (target, type); a message's 0x04, 0x08, 0x10 (type), 0x14 (set type),
// 0x24, 0x2C, 0x38, 0x3C, 0x40, 0x80, 0x84; the stream's 0x1C, 0x20, 0x24,
// 0x48 (uint16, sint32, uint32, GetError); SC4Stream's WriteReference,
// WriteBool (stream, byte), ReadReference (stream, iid, out, 0), ReadBool
// (stream, out), cdecl; the city's 0x110, 0x114, 0xAC..0xB8, 0xBC, 0xC4,
// 0xD0; the common base's functions at their addresses; SC4String's
// FromText, SC4List's Destroy2 and Clear2, the node allocator (as
// game_access.h's ListNodeAllocator), the stopwatch's Start, the camera
// view's ZoomAndRotation (zoom out, rotation out), SC4RouteLine's Unprepare,
// the overlay's 0x007DC2C0 and the service 0x00602600.

constexpr std::uintptr_t kRouteLine = 0x00B22970;          // SC4RouteLine*, the vehicle manager's
constexpr std::uintptr_t kFollowed = 0x00B21B8C;           // the automaton the camera follows
constexpr std::uintptr_t kFollowLocked = 0x00B21B94;       // byte
constexpr std::uintptr_t kVehicleMost = 0x00B21BC8;        // uint32, tuning
constexpr std::uintptr_t kPedestrianMost = 0x00B21BDC;     // uint32, tuning
constexpr std::uintptr_t kVehicleTuningByte = 0x00B21CA4;  // byte, tuning
constexpr std::uintptr_t kPedestrianTuningByte = 0x00B21CA5;
constexpr std::uintptr_t kVehicleRound = 0x00A9D0B8;       // float 1.5 x 2^23
constexpr std::uintptr_t kPedestrianRound = 0x00A9A40C;    // float 1.5 x 2^23
constexpr std::uintptr_t kZeroPosition = 0x00B228DC;       // float, filled at startup
constexpr std::uintptr_t kPropertyThreshold = 0x00B22974;  // float, filled at startup
constexpr std::uintptr_t kTemplateName = 0x00B0A0D0;       // const char*
constexpr std::uintptr_t kControllerNames = 0x00B0A0BC;    // const char*[], null-ended
constexpr std::uintptr_t kVehiclePool = 0x00B0A0F8, kPedestrianPool = 0x00B0A018; // +4 the current block
constexpr std::uint32_t kIidOccupant = 0x26BB767E;

// The stream's slots.
namespace stream {
inline constexpr std::size_t kUint16 = 0x1C, kSint32 = 0x20, kUint32 = 0x24, kError = 0x48;
} // namespace stream

// A message's slots.
namespace message {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kType = 0x10, kSetType = 0x14;
} // namespace message

template <typename T>
inline T& GlobalAt(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}

void* City() { return Global<void*>(addr::SC4_City); }

// A string object on the stack as the game's SC4String (vtable 0x00A80810):
// begin, end and capacity end of a heap buffer, a word after.
struct LocalString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t word;
};

// ------------------------------------------------------------------------------------
// The life cycle and the tuning

// 0x0054DBA0, slot 3 (base).
std::uint32_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("base Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    CallThis<std::uint32_t>(addr::cSC4AutomataManager_Init, self);
    At<std::uint32_t>(self, field::kA9C) = 0xFFFFFFFFu;
    At<std::uint32_t>(self, field::kAA0) = 0xFFFFFFFFu;
    if (void* const city = City()) {
        void* const object = Virtual<void*>(city, 0x114);
        At<void*>(self, field::kA88) = object;
        Virtual<std::uint32_t>(object, slot::kAddRef);
    }
    CallThis<std::uint32_t>(addr::SC4Stopwatch_Start, self + field::kStopwatch);
    return 1;
}

constexpr std::uint32_t kVehicleMessages[] = {0xC53D10AAu, 0xA6B79601u, 0xA6B79611u, 0xC9DA96E8u,
                                              0xC9DA96E9u, 0x44778234u, 0x0772FAD4u};
constexpr std::uint32_t kPedestrianMessages[] = {0xA9E3B6E2u};

// The derived Inits: the base's, the message target added for each type,
// then slots 24 and 58.
template <std::size_t N>
void InitOf(char* self, const std::uint32_t (&types)[N])
{
    if (At<std::uint8_t>(self, field::kInitialized))
        return;
    CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_Init, self);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const std::uint32_t type : types)
            Virtual<std::uint8_t>(server, 0x14, self + field::kMessage, type);
    Virtual<std::uint32_t>(self, slot::kHandleTuning);
    Virtual<std::uint32_t>(self, slot::kTuningSetUp);
}

// 0x0055B7E0, the vehicle manager's slot 3.
std::uint32_t __fastcall VehicleInit(char* self, void*)
{
    static bool logged;
    FirstCall("vehicle Init", logged);
    InitOf(self, kVehicleMessages);
    return 1;
}

// 0x00544820, the pedestrian manager's slot 3.
std::uint32_t __fastcall PedestrianInit(char* self, void*)
{
    static bool logged;
    FirstCall("pedestrian Init", logged);
    InitOf(self, kPedestrianMessages);
    return 1;
}

// 0x00552A20, on the record list field (+0xAB8): each node's object given
// back (to the controller manager's slot 0x38 first when +0x38 is set) and
// released, +0x3C zeroed; then the list cleared (the game tail-jumps).
void* __fastcall ReleaseRecordList(char** list, void*)
{
    static bool logged;
    FirstCall("ReleaseRecordList", logged);
    for (char* node = At<char*>(*list, 0); node != *list; node = At<char*>(node, 0)) {
        if (At<std::uint8_t>(node, record::kHeld) && At<void*>(node, record::kObject)) {
            void* const controllers = Virtual<void*>(City(), 0xBC);
            Virtual<std::uint32_t>(controllers, 0x38, At<void*>(node, record::kObject));
            At<std::uint8_t>(node, record::kHeld) = 0;
        }
        if (void* const object = At<void*>(node, record::kObject)) {
            At<void*>(node, record::kObject) = nullptr;
            Virtual<std::uint32_t>(object, slot::kRelease);
        }
        At<std::uint32_t>(node, record::kD) = 0;
    }
    return CallThis<void*>(addr::cSC4VehPedManagerBase_ClearRecordList, list);
}

// 0x00552D00, the base's Shutdown: answers the common base's (the game
// tail-jumps to it, initialized or not).
std::uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("base Shutdown", logged);
    if (At<std::uint8_t>(self, field::kInitialized)) {
        if (void* const object = At<void*>(self, field::kA88)) {
            Virtual<std::uint32_t>(object, slot::kRelease);
            At<void*>(self, field::kA88) = nullptr;
        }
        for (char* r = At<char*>(self, field::kRecordsBegin); r != At<char*>(self, field::kRecordsEnd); r += 0x1C)
            if (void* const object = At<void*>(r, 4))
                Virtual<std::uint32_t>(object, slot::kRelease);
        // erase(begin, end): the records after the end, (end - end) / 0x1C of
        // them, would move down to begin; none do.
        At<char*>(self, field::kRecordsEnd) = At<char*>(self, field::kRecordsBegin);
        CallThis<void*>(addr::cSC4VehPedManagerBase_ReleaseRecordList, self + field::kRecordList);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_Shutdown, self);
}

// 0x00562D60, on the vehicle manager's +0xAE0 object: with +0xC set, the
// overlay stopped (0x007DC2C0) and the service 0x00602600's slot 0x18 told
// (+0x50, 0xC0).
void __fastcall OverlayStop(char* object, void*)
{
    static bool logged;
    FirstCall("OverlayStop", logged);
    if (!At<std::uint8_t>(object, 0xC))
        return;
    CallThis<void>(addr::SC4UI_MapOverlay_Stop, object);
    void* const service = CallCdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<std::uint32_t>(service, 0x18, object + 0x50, 0xC0u);
}

template <std::size_t N>
void RemoveTargets(char* self, const std::uint32_t (&types)[N])
{
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const std::uint32_t type : types)
            Virtual<std::uint8_t>(server, 0x18, self + field::kMessage, type);
}

// 0x0055B880, the vehicle manager's slot 4.
std::uint32_t __fastcall VehicleShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("vehicle Shutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    if (void* const line = Global<void*>(kRouteLine)) {
        Virtual<std::uint32_t>(Global<void*>(addr::SC4_View43DD0), 0x84, line);
        CallThis<void>(addr::SC4RouteLine_Unprepare, Global<void*>(kRouteLine));
        Virtual<std::uint32_t>(Global<void*>(kRouteLine), slot::kRelease);
        GlobalAt<void*>(kRouteLine) = nullptr;
    }
    if (At<void*>(self, field::kVehicleAE0)) {
        CallThis<void>(addr::cSC4VehicleManager_OverlayStop, At<void*>(self, field::kVehicleAE0));
        Virtual<std::uint32_t>(At<void*>(self, field::kVehicleAE0), slot::kRelease);
        At<void*>(self, field::kVehicleAE0) = nullptr;
    }
    RemoveTargets(self, kVehicleMessages);
    CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_Shutdown, self);
    return 1;
}

// 0x00544860, the pedestrian manager's slot 4.
std::uint32_t __fastcall PedestrianShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("pedestrian Shutdown", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    RemoveTargets(self, kPedestrianMessages);
    CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_Shutdown, self);
    return 1;
}

// 0x0055B410 and 0x00544410, slot 24: the most automata from the tuning's
// word, +0xA3C from its byte.
std::uint32_t __fastcall VehicleHandleTuning(char* self, void*)
{
    static bool logged;
    FirstCall("vehicle HandleTuningParametersChanged", logged);
    Virtual<std::uint32_t>(self, slot::kSetMaxCount, Global<std::uint32_t>(kVehicleMost));
    At<std::uint8_t>(self, field::kTuningByte) = Global<std::uint8_t>(kVehicleTuningByte);
    return 1;
}

std::uint32_t __fastcall PedestrianHandleTuning(char* self, void*)
{
    static bool logged;
    FirstCall("pedestrian HandleTuningParametersChanged", logged);
    Virtual<std::uint32_t>(self, slot::kSetMaxCount, Global<std::uint32_t>(kPedestrianMost));
    At<std::uint8_t>(self, field::kTuningByte) = Global<std::uint8_t>(kPedestrianTuningByte);
    return 1;
}

// Slot 58: the tuning's word times the app object's float, rounded the
// game's way, as the most automata; +0xA3C from the app's byte less one;
// slot 49 with the camera view's zoom and -1.
void TuningSetUpOf(char* self, std::uintptr_t most, std::size_t factorAt, std::uintptr_t round, std::size_t byteAt)
{
    char* const app = Global<char*>(addr::SC4_App43CC4);
    if (!app)
        return;
    double value = static_cast<double>(Global<std::uint32_t>(most)); // FILD, plus 2^32 when negative
    value *= static_cast<double>(At<float>(At<char*>(app, 0x24), factorAt));
    value += static_cast<double>(Global<float>(round));
    const float sum = static_cast<float>(value);
    std::uint32_t bits;
    std::memcpy(&bits, &sum, sizeof bits);
    Virtual<std::uint32_t>(self, slot::kSetMaxCount, bits + 0xB4C00000u);
    At<std::uint8_t>(self, field::kTuningByte) =
        static_cast<std::uint8_t>(At<std::uint8_t>(At<char*>(app, 0x18), byteAt) - 1);
    std::uint32_t zoom = bits, rotation = 0; // both written by the call
    CallThis<void>(addr::SC4CameraView_ZoomAndRotation, Global<void*>(addr::SC4_CameraView), &zoom, &rotation);
    Virtual<std::uint32_t>(self, slot::kSlot49, zoom, 0xFFFFFFFFu);
}

// 0x0055C9D0, the vehicle manager's slot 58.
std::uint32_t __fastcall VehicleTuningSetUp(char* self, void*)
{
    static bool logged;
    FirstCall("vehicle TuningSetUp", logged);
    TuningSetUpOf(self, kVehicleMost, 0x1FC, kVehicleRound, 0x224);
    return 1;
}

// 0x005450A0, the pedestrian manager's slot 58.
std::uint32_t __fastcall PedestrianTuningSetUp(char* self, void*)
{
    static bool logged;
    FirstCall("pedestrian TuningSetUp", logged);
    TuningSetUpOf(self, kPedestrianMost, 0x224, kPedestrianRound, 0x24C);
    return 1;
}

// 0x0055B3F0, the vehicle manager's +4 slot 3 (this = object + 4), ret 4:
// the common base's 0x0050DD30, its AL made 0 or 1.
std::uint32_t __fastcall VehicleDoMessage4(char* part, void*, void* message)
{
    static bool logged;
    FirstCall("vehicle DoMessage4", logged);
    const std::uint32_t answer = CallThis<std::uint32_t>(addr::cSC4AutomataManager_DoMessage4, part, message);
    return (answer & 0xFFFFFF00u) | ((answer & 0xFF) ? 1u : 0u);
}

// ------------------------------------------------------------------------------------
// The messages

// Slot 0x2C0 of every automaton of the list (+0x44), (a, b).
void TellAutomata(char* self, std::uint32_t a, std::uint32_t b)
{
    for (char* node = At<char*>(At<char*>(self, field::kAutomata), 0); node != At<char*>(self, field::kAutomata);
         node = At<char*>(node, 0))
        Virtual<std::uint32_t>(At<void*>(node, 8), 0x2C0, a, b);
}

// 0x00562620, the vehicle manager's message target (this = object + 0xC),
// ret 4.
std::uint32_t __fastcall VehicleDoMessage(char* part, void*, void* msg)
{
    static bool logged;
    FirstCall("vehicle DoMessage", logged);
    char* const self = part - field::kMessage;
    const std::uint32_t type = Virtual<std::uint32_t>(msg, message::kType);
    if (type == 0xA6B79601u || type == 0xA6B79611u) {
        void* const followed = Global<void*>(kFollowed);
        if (followed && !Global<std::uint8_t>(kFollowLocked) && Virtual<std::uint32_t>(followed, 0xE4, 0x10u) == 0)
            Virtual<std::uint32_t>(self, slot::kSetCameraFollow, 0u);
    } else if (type == 0x0772FAD4u) {
        CallThis<std::uint32_t>(addr::cSC4VehicleManager_UpdateControllers, self);
    } else if (type == 0x44778234u) {
        if (void* const clock = Global<void*>(addr::SC4_Clock43DBC)) {
            const std::uint32_t value = Virtual<std::uint32_t>(msg, 0x38);
            if (value == 0) {
                if (Virtual<std::uint32_t>(clock, 0x18) == 2)
                    TellAutomata(self, 1, 0);
            } else if (value == 1) {
                if (Virtual<std::uint32_t>(clock, 0x18) == 1)
                    TellAutomata(self, 0, 0);
            }
        }
    } else if (type == 0xC9DA96E8u) {
        TellAutomata(self, 0, 1);
    } else if (type == 0xC9DA96E9u) {
        TellAutomata(self, 1, 1);
    } else if (type == 0xC53D10AAu) {
        const std::uint32_t id = Virtual<std::uint32_t>(msg, 0x24);
        Virtual<std::uint32_t>(msg, 0x38);
        if (id == 0xA998D30Bu) {
            CallCdecl<void>(addr::cSC4AutomataManager_LoadTuning);
            if (City()) {
                for (const std::size_t at : {std::size_t{0xAC}, std::size_t{0xB0}, std::size_t{0xB4}, std::size_t{0xB8}})
                    if (Virtual<void*>(City(), at))
                        Virtual<std::uint32_t>(Virtual<void*>(City(), at), slot::kHandleTuning);
                if (Virtual<void*>(City(), 0xC4))
                    Virtual<std::uint32_t>(Virtual<void*>(City(), 0xC4), 0x7C);
            }
            if (void* const cells = At<void*>(self, field::kCells))
                Virtual<std::uint32_t>(cells, 0x14);
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_DoMessage, part, msg);
}

// A property's flag for 0x00561500: the table answers the property of the
// occupant's slot 0x18 part (slot 0x70) and its value (slot 0x68, a float)
// is above the threshold. The game pushes the property before it asks for
// the part, which it does twice.
std::uint32_t PropertyFlag(void* table, void* occupant, std::uint32_t property)
{
    if (!Virtual<std::uint8_t>(table, 0x70, Virtual<void*>(occupant, 0x18), property))
        return 0;
    const float* const value = Virtual<const float*>(table, 0x68, Virtual<void*>(occupant, 0x18), property);
    return *value > Global<float>(kPropertyThreshold) ? 1u : 0u;
}

char* NewListHead()
{
    char* const head = Virtual<char*>(ListNodeAllocator(), 0x0C, 0x0Cu);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    return head;
}

// The game's inline string from a C string: a buffer of the length plus one
// from the STL allocator, the text and a zero.
void MakeString(LocalString& s, const char* text)
{
    s.begin = s.end = s.capacity = nullptr;
    const std::size_t length = std::strlen(text);
    // The game throws a length error when length + 1 wraps to 0, which a C
    // string cannot reach.
    char* const buffer = static_cast<char*>(Allocate(static_cast<std::uint32_t>(length + 1)));
    s.begin = buffer;
    s.end = buffer;
    s.capacity = buffer + length + 1;
    if (length) {
        std::memmove(buffer, text, length);
        s.end = buffer + length;
    }
    *s.end = 0;
}

// 0x00561500, the vehicle manager's controller update (message 0x0772FAD4).
std::uint32_t __fastcall VehicleUpdateControllers(char* self, void*)
{
    static bool logged;
    FirstCall("vehicle UpdateControllers", logged);
    void* const simulator = Virtual<void*>(City(), 0x110);
    void* const tables = Virtual<void*>(City(), 0xD0);
    if (!simulator || !tables)
        return 1;
    LocalString name{};
    CallThis<void*>(addr::SC4String_FromText, &name, Global<const char*>(kTemplateName));
    void* const tmpl = Virtual<void*>(At<void*>(self, field::kScripts), 0x44, &name);
    name.vtable = addr::SC4String_Vtable;
    if (name.begin)
        Free(name.begin);
    if (tmpl) {
        char* list = NewListHead();
        Virtual<std::uint32_t>(simulator, 0xC4, &list, 0u);
        void* const table = Virtual<void*>(tables, 0x70, 0xAA538CB3u);
        for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
            void* const occupant = At<void*>(node, 8);
            const std::uint32_t active = PropertyFlag(table, occupant, 0xEA56549Eu);
            if (void* const controller = Virtual<void*>(At<void*>(self, field::kControllers), 0x48, occupant, tmpl))
                Virtual<std::uint32_t>(controller, 0x48, active); // SetActive
            Virtual<std::uint32_t>(occupant, slot::kRelease);
        }
        CallThis<void>(addr::SC4List_Destroy2, &list);
    }
    char* list = NewListHead();
    Virtual<std::uint32_t>(simulator, 0xBC, &list, 0u);
    void* const table = Virtual<void*>(tables, 0x70, 0x4A538CC6u);
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        const std::uint32_t active = PropertyFlag(table, occupant, 0x4A5654BAu);
        for (std::uintptr_t at = kControllerNames; const char* const text = Global<const char*>(at); at += 4) {
            LocalString s{};
            MakeString(s, text);
            s.vtable = addr::SC4String_Vtable;
            s.word = 0;
            void* const controller = Virtual<void*>(At<void*>(self, field::kControllers), 0x4C, occupant, &s);
            if (s.begin)
                Free(s.begin);
            if (controller)
                Virtual<std::uint32_t>(controller, 0x48, active); // SetActive
        }
        Virtual<std::uint32_t>(occupant, slot::kRelease);
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    Virtual<std::uint32_t>(ListNodeAllocator(), 0x10, list); // the head back
    return 1;
}

// 0x005448D0, on a pedestrian (position, value), ret 8: nothing (AL 0) in
// group 0x4000; +0x254 2 for a value of 0, else 1; slot 0x90 (+0x80, 1.0); slot
// 0x40 (+0x40, the offset to the position in x and z).
std::uint32_t __fastcall PedestrianSendTo(char* automaton, void*, const float* position, std::uint32_t value)
{
    static bool logged;
    FirstCall("pedestrian SendTo", logged);
    const std::uint32_t grouped = Virtual<std::uint32_t>(automaton, 0x7C, 0x4000u);
    if (grouped & 0xFF)
        return grouped & 0xFFFFFF00u;
    At<std::uint32_t>(automaton, 0x254) = (value == 0 ? 1u : 0u) + 1u;
    Virtual<std::uint32_t>(automaton, 0x90, At<std::uint32_t>(automaton, 0x80), 0x3F800000u); // 1.0f
    float offset[3];
    offset[0] = static_cast<float>(static_cast<double>(position[0]) - static_cast<double>(At<float>(automaton, 0x40)));
    offset[1] = 0.0f;
    offset[2] = static_cast<float>(static_cast<double>(position[2]) - static_cast<double>(At<float>(automaton, 0x48)));
    const std::uint32_t moved = Virtual<std::uint32_t>(automaton, 0x40, automaton + 0x40, static_cast<float*>(offset));
    return (moved & 0xFFFFFF00u) | 1u;
}

float FloatOf(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::uint32_t BitsOf(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// 0x00544F30, the pedestrian manager's (occupant, value, distance), ret 0xC.
std::uint32_t __fastcall PedestrianGather(char* self, void*, void* occupant, std::uint32_t value, std::uint32_t distance)
{
    static bool logged;
    FirstCall("pedestrian Gather", logged);
    const std::uint32_t zero = Global<std::uint32_t>(kZeroPosition);
    std::uint32_t position[3] = {zero, zero, zero};
    std::uint32_t there[3] = {zero, zero, zero};
    std::uint32_t count = 0;
    Virtual<std::uint32_t>(occupant, 0x20, position);
    char* node = At<char*>(At<char*>(self, field::kAutomata), 0);
    if (node != At<char*>(self, field::kAutomata)) {
        const float radius = static_cast<float>(static_cast<double>(distance)); // FILD, plus 2^32 when negative
        do {
            char* const automaton = At<char*>(node, 8);
            node = At<char*>(node, 0);
            Virtual<std::uint32_t>(automaton, 0x1C, there);
            const double dx = static_cast<double>(FloatOf(there[0])) - static_cast<double>(FloatOf(position[0]));
            const double dy = static_cast<double>(FloatOf(there[1])) - static_cast<double>(FloatOf(position[1]));
            const double dz = static_cast<double>(FloatOf(there[2])) - static_cast<double>(FloatOf(position[2]));
            there[0] = BitsOf(static_cast<float>(dx));
            there[1] = BitsOf(static_cast<float>(dy));
            there[2] = BitsOf(static_cast<float>(dz));
            const double length = std::sqrt(dz * dz + dy * dy + dx * dx);
            if (length < static_cast<double>(radius) && !Virtual<std::uint8_t>(automaton, 0x7C, 0x4000u) &&
                (CallThis<std::uint32_t>(addr::cSC4PedestrianManager_SendTo, automaton,
                                         reinterpret_cast<const float*>(position), value) &
                 0xFF))
                ++count;
        } while (node != At<char*>(self, field::kAutomata));
    }
    void* const raw = CallCdecl<void*>(addr::RZ_operator_new, static_cast<std::size_t>(0x2C));
    void* const msg = raw ? CallThis<void*>(addr::cGZMessage_cGZMessage, raw) : nullptr;
    Virtual<std::uint32_t>(msg, message::kAddRef);
    Virtual<std::uint32_t>(msg, message::kSetType, 0xA9E3B6E3u);
    Virtual<std::uint32_t>(msg, 0x84, occupant);
    Virtual<std::uint32_t>(msg, 0x2C, value);
    Virtual<std::uint32_t>(msg, 0x40, count);
    Virtual<std::uint32_t>(Global<void*>(addr::SC4_MessageServer), 0x10, msg, 0u);
    Virtual<std::uint32_t>(msg, message::kRelease);
    return 1;
}

// 0x00545450, the pedestrian manager's message target (this = object +
// 0xC), ret 4.
std::uint32_t __fastcall PedestrianDoMessage(char* part, void*, void* msg)
{
    static bool logged;
    FirstCall("pedestrian DoMessage", logged);
    if (Virtual<std::uint32_t>(msg, message::kType) == 0x230E27ACu) {
        Virtual<std::uint32_t>(msg, 0x3C);
        return Virtual<std::uint32_t>(msg, 0x24) & 0xFFFFFF00u;
    }
    if (Virtual<std::uint32_t>(msg, message::kType) != 0xA9E3B6E2u)
        return CallThis<std::uint32_t>(addr::cSC4AutomataManager_DoMessage, part, msg);
    void* occupant = nullptr; // the game tests it for a release here, still null
    void* const object = Virtual<void*>(msg, 0x80);
    Virtual<std::uint8_t>(object, 0x00, kIidOccupant, &occupant);
    const std::uint32_t distance = Virtual<std::uint32_t>(msg, 0x38);
    const std::uint32_t value = Virtual<std::uint32_t>(msg, 0x24);
    std::uint32_t answer = CallThis<std::uint32_t>(addr::cSC4PedestrianManager_Gather, part - field::kMessage,
                                                   occupant, value, distance);
    if (occupant)
        answer = (Virtual<std::uint32_t>(occupant, slot::kRelease) & 0xFFFFFF00u) | (answer & 0xFF);
    return answer;
}

// ------------------------------------------------------------------------------------
// The save

// A version read into the stack slot of the stream argument, as the game
// does: a failed read leaves the slot's low word as the stream's address.
// The call's answer goes to `answer` (the failed tests keep its top bytes).
std::uint32_t ReadVersion(void* s, std::uint32_t& answer)
{
    std::uint32_t slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(s));
    answer = Virtual<std::uint32_t>(s, stream::kUint16, &slot);
    return slot;
}

std::uint32_t NoError(void* s) { return Virtual<std::uint8_t>(s, stream::kError) == 0 ? 1u : 0u; }

// 0x00552A80, on the record list field (+0xAB8): (stream), ret 4.
std::uint32_t __fastcall WriteRecordList(char** list, void*, void* s)
{
    static bool logged;
    FirstCall("WriteRecordList", logged);
    Virtual<std::uint8_t>(s, stream::kUint16, 1u);
    std::uint32_t count = 0;
    for (char* node = At<char*>(*list, 0); node != *list; node = At<char*>(node, 0))
        ++count;
    Virtual<std::uint8_t>(s, stream::kUint32, count);
    for (char* node = At<char*>(*list, 0); node != *list; node = At<char*>(node, 0)) {
        Virtual<std::uint8_t>(s, stream::kSint32, At<std::uint32_t>(node, record::kA));
        Virtual<std::uint8_t>(s, stream::kSint32, At<std::uint32_t>(node, record::kB));
        Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(node, record::kC));
        for (std::size_t at = record::kRect1; at < record::kObject; at += 4)
            Virtual<std::uint8_t>(s, stream::kSint32, At<std::uint32_t>(node, at));
        CallCdecl<std::uint32_t>(addr::SC4Stream_WriteReference, s, At<void*>(node, record::kObject));
        CallCdecl<std::uint32_t>(addr::SC4Stream_WriteBool, s,
                                 static_cast<std::uint32_t>(At<std::uint8_t>(node, record::kHeld)));
        Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(node, record::kD));
    }
    return NoError(s);
}

// 0x00553570, the serializable part's Write (this = object + 8), ret 4.
std::uint32_t __fastcall Write(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<std::uint8_t>(s, stream::kUint16, 3u);
    const std::uint32_t base = CallThis<std::uint32_t>(addr::cSC4AutomataManager_Write, part, s);
    if (!(base & 0xFF))
        return base;
    Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(self, field::kDefaultClass));
    char* const current = At<char*>(self, field::kFirstKind);
    Virtual<std::uint8_t>(s, stream::kUint32,
                          current == At<char*>(self, field::kKinds) ? 0u : At<std::uint32_t>(current, 8));
    return CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_WriteRecordList, self + field::kRecordList, s);
}

// 0x00553E90, on the record list field: (stream), ret 4. Version 1 only;
// each record read into a zeroed record on the stack and appended as a new
// node (copied by 0x0054ED10).
std::uint32_t __fastcall ReadRecordList(char** list, void*, void* s)
{
    static bool logged;
    FirstCall("ReadRecordList", logged);
    std::uint32_t answer;
    if (static_cast<std::uint16_t>(ReadVersion(s, answer)) != 1)
        return answer & 0xFFFFFF00u;
    std::uint32_t count = 0; // stack leftovers in the game when the read fails
    Virtual<std::uint8_t>(s, stream::kUint32, &count);
    for (std::uint32_t i = 0; i < count; ++i) {
        alignas(4) char r[0x38] = {}; // as node + 8 onwards
        Virtual<std::uint8_t>(s, stream::kSint32, r + 0x00);
        Virtual<std::uint8_t>(s, stream::kSint32, r + 0x04);
        Virtual<std::uint8_t>(s, stream::kUint32, r + 0x08);
        for (std::size_t at = 0x0C; at < 0x2C; at += 4)
            Virtual<std::uint8_t>(s, stream::kSint32, r + at);
        if (void* const object = At<void*>(r, 0x2C)) { // zeroed above
            At<void*>(r, 0x2C) = nullptr;
            Virtual<std::uint32_t>(object, slot::kRelease);
        }
        CallCdecl<std::uint32_t>(addr::SC4Stream_ReadReference, s, kIidOccupant, r + 0x2C, 0u);
        CallCdecl<std::uint32_t>(addr::SC4Stream_ReadBool, s, r + 0x30);
        Virtual<std::uint8_t>(s, stream::kUint32, r + 0x34);
        char* const head = *list;
        char* const node = static_cast<char*>(Allocate(record::kSize));
        if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0)
            CallThis<void*>(addr::cSC4VehPedManagerBase_CopyRecord, node + 8, static_cast<char*>(r));
        char* const last = At<char*>(head, 4);
        At<char*>(node, 0) = head;
        At<char*>(node, 4) = last;
        At<char*>(last, 0) = node;
        At<char*>(head, 4) = node;
        if (void* const object = At<void*>(r, 0x2C))
            Virtual<std::uint32_t>(object, slot::kRelease);
    }
    return NoError(s);
}

// 0x00554410, the serializable part's Read (this = object + 8), ret 4.
std::uint32_t __fastcall Read(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    std::uint32_t answer;
    const std::uint16_t version = static_cast<std::uint16_t>(ReadVersion(s, answer));
    if (version != 2 && version != 3)
        return (answer & 0xFFFF0000u) | (version & 0xFF00u);
    const std::uint32_t base = CallThis<std::uint32_t>(addr::cSC4AutomataManager_Read, part, s);
    if (!(base & 0xFF))
        return base;
    std::uint32_t dropped = 0; // the saved +0xA8C, read and not kept
    Virtual<std::uint8_t>(s, stream::kUint32, &dropped);
    At<char*>(self, field::kFirstKind) = At<char*>(At<char*>(self, field::kKinds), 0);
    std::uint32_t kind = 0; // stack leftovers in the game when the read fails
    Virtual<std::uint8_t>(s, stream::kUint32, &kind);
    if (kind != 0)
        while (At<char*>(self, field::kFirstKind) != At<char*>(self, field::kKinds) &&
               At<std::uint32_t>(At<char*>(self, field::kFirstKind), 8) != kind)
            At<char*>(self, field::kFirstKind) = At<char*>(At<char*>(self, field::kFirstKind), 0);
    if (version > 2)
        return CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_ReadRecordList, self + field::kRecordList, s);
    return (kind & 0xFFFFFF00u) | (base & 0xFF);
}

// ------------------------------------------------------------------------------------
// Adding and removing

// The derived AddAutomaton (automaton), ret 4: with none, a new one from the
// class's pool when the count is below the most.
std::uint32_t AddAutomatonOf(char* self, void* automaton, std::uintptr_t pool, std::uintptr_t construct)
{
    if (!automaton) {
        const std::uint32_t count = At<std::uint32_t>(self, field::kCount);
        if (count >= At<std::uint32_t>(self, field::kMaxCount))
            return count & 0xFFFFFF00u;
        static bool logged;
        FirstCall("AddAutomaton making one", logged);
        char* block = At<char*>(Global<char*>(pool + 4), 8);
        if (!block)
            block = CallThis<char*>(addr::SC4EdgePool_Grow, reinterpret_cast<void*>(pool));
        At<char*>(Global<char*>(pool + 4), 8) = At<char*>(block, 0);
        ++At<std::uint32_t>(Global<char*>(pool + 4), 0xC);
        // The manager and three zero words (a 12-byte value).
        automaton = CallThis<void*>(construct, block, self, 0u, 0u, 0u);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_AddAutomaton, self, automaton);
}

// 0x00562C10, the vehicle manager's slot 7.
std::uint32_t __fastcall VehicleAddAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("vehicle AddAutomaton", logged);
    return AddAutomatonOf(self, automaton, kVehiclePool, addr::SC4VehPedManager_NewVehicle);
}

// 0x00546990, the pedestrian manager's slot 7.
std::uint32_t __fastcall PedestrianAddAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("pedestrian AddAutomaton", logged);
    return AddAutomatonOf(self, automaton, kPedestrianPool, addr::SC4VehPedManager_NewPedestrian);
}

// 0x0054D690, slot 8 (automaton, b), ret 8: the common base's, then slot 69;
// AL the common base's answer, the rest of EAX slot 69's.
std::uint32_t __fastcall RemoveAutomaton(char* self, void*, void* automaton, std::uint32_t b)
{
    static bool logged;
    FirstCall("RemoveAutomaton", logged);
    const std::uint32_t removed =
        CallThis<std::uint32_t>(addr::cSC4AutomataManager_RemoveAutomaton, self, automaton, b);
    const std::uint32_t after = Virtual<std::uint32_t>(self, slot::kForgetAutomaton, automaton);
    return (after & 0xFFFFFF00u) | (removed & 0xFF);
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

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Init, Init),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_Init, VehicleInit),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_Init, PedestrianInit),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_Shutdown, VehicleShutdown),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_Shutdown, PedestrianShutdown),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_ReleaseRecordList, ReleaseRecordList),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_OverlayStop, OverlayStop),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_HandleTuningParametersChanged, VehicleHandleTuning),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_HandleTuningParametersChanged, PedestrianHandleTuning),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_TuningSetUp, VehicleTuningSetUp),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_TuningSetUp, PedestrianTuningSetUp),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_DoMessage4, VehicleDoMessage4),
};

const Detour kMessages[] = {
    OPENSC4_DETOUR(addr::cSC4VehicleManager_DoMessage, VehicleDoMessage),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_UpdateControllers, VehicleUpdateControllers),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_DoMessage, PedestrianDoMessage),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_Gather, PedestrianGather),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_SendTo, PedestrianSendTo),
};

const Detour kSerial[] = {
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Write, Write),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_WriteRecordList, WriteRecordList),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Read, Read),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_ReadRecordList, ReadRecordList),
};

const Detour kAdd[] = {
    OPENSC4_DETOUR(addr::cSC4VehicleManager_AddAutomaton, VehicleAddAutomaton),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_AddAutomaton, PedestrianAddAutomaton),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_RemoveAutomaton, RemoveAutomaton),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

bool InstallLife(registry::Mode) { return InstallList(kLife, sizeof kLife / sizeof kLife[0], "life"); }

bool InstallMessages(registry::Mode)
{
    return InstallList(kMessages, sizeof kMessages / sizeof kMessages[0], "messages");
}

bool InstallSerial(registry::Mode) { return InstallList(kSerial, sizeof kSerial / sizeof kSerial[0], "serial"); }

bool InstallAdd(registry::Mode) { return InstallList(kAdd, sizeof kAdd / sizeof kAdd[0], "add"); }

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
    registry::Register({"simulators.automata.vehmgr.life", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehPedManagerBase_Init,
                        "Our vehicle and pedestrian managers' life cycle: Init and Shutdown (base, vehicle, "
                        "pedestrian), the record list's release, the overlay's stop, HandleTuningParametersChanged "
                        "and the tuning set-up (slot 58) of both, and the vehicle manager's +4 slot 3 (13 detours; "
                        "part v2, src/core/automata_vehmgr.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.automata.vehmgr.messages", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehicleManager_DoMessage,
                        "Our vehicle and pedestrian managers' messages: both message targets, the vehicle manager's "
                        "controller update and the pedestrian manager's gathering with the pedestrian's send (5 "
                        "detours; part v2, src/core/automata_vehmgr.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.automata.vehmgr.serial", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehPedManagerBase_Write,
                        "Our vehicle and pedestrian managers' save: Write and Read of the serializable part and of "
                        "the record list (4 detours; part v2, src/core/automata_vehmgr.cpp)",
                        false, InstallSerial}),
    registry::Register({"simulators.automata.vehmgr.add", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehPedManagerBase_RemoveAutomaton,
                        "Our vehicle and pedestrian managers' AddAutomaton (both) and RemoveAutomaton (3 detours; "
                        "part v2, src/core/automata_vehmgr.cpp)",
                        false, InstallAdd}),
};

} // namespace
} // namespace opensc4::automata_vehmgr
