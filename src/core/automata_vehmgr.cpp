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
//   simulators.automata.vehmgr.create and .frame: part v3's 29 (creation,
//   dispatch and frames), in kCreate and kFrame; described at "Part v3".
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
// SC4PairList::Destroy, SC4Curve::Evaluate and the allocators.
#include "core/automata_vehmgr.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/sim_timers.h"

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
    return AddAutomatonOf(self, automaton, kVehiclePool, addr::cSC4Vehicle_Construct);
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
// Part v3: creation, dispatch and frames
//
// Two more groups, off by default, so the groups of parts v1 and v2 stay as
// they were:
//   simulators.automata.vehmgr.create (kCreate, 11): GetRandomCreationPoint
//   (slot 16; 0x00553080), the two Dispatch (slots 17 and 18; 0x00551730,
//   0x0054DBF0), the record add (slot 70; 0x005538D0), the occupant's
//   automaton (slot 71; 0x0054F8A0) with the occupant position 0x0054DD50,
//   the kind matrix (slot 72; 0x0054DDB0), the automaton's kind set
//   0x0054E690, and the three spawns (slots 79, 80 and 89; 0x0054EF00,
//   0x0054F3C0, 0x0054FA10).
//   simulators.automata.vehmgr.frame (kFrame, 18): the occupant's removal
//   (slot 52; 0x0054ED90) and arrival (slot 53; vehicle 0x0055B480,
//   pedestrian 0x00544300), the records' removal (slot 55; 0x005516C0) with
//   the record copy 0x0054DB50, the occupants' refresh (slot 56; 0x0054F7B0),
//   the frame update (slot 63; base 0x00553300, vehicle 0x0055B600) with the
//   record list's update 0x00552BD0, the automaton's forgetting (slot 69;
//   0x0054FBC0), the kind sweep (slot 76; 0x00552000), the cell balance (slot
//   77; 0x005554E0) with the tuning map's 0x00555350 and 0x00555400, the cell
//   spawn (slot 78; 0x00551A50), the cell cull (slot 81; 0x005539A0), the lot
//   sweep (slot 83; 0x00553370) and the records' update (slot 84; 0x00552DE0).
//
// What it does. Every frame (slot 63 with the frame's time) the record list
// (+0xAB8) is aged at most every 500 ms (0x00552BD0: a record past its time
// is given back and freed, the others follow their object's cell), then,
// while the manager may have automata (+0x3C above 0, +0x4D and 0x00B21B9C
// clear), the kind sweep (slot 76, when +0x4C is set) and the records'
// update (slot 84, when the +0xAA8 vector has records), then the common
// base's slot 63 0x0050A7B0; the vehicle manager adds slot 64. The kind
// sweep adds the time to each kind's clock, picks the next kind of the mask
// 0x00B21D5C, and for kinds the day or night (0x00B43DC4's slot 0x58) allows
// walks the cells of the cells object's rectangle from where it stopped
// (+0xA9C, +0xAA0) for at most 5 ms of its own stopwatch: kinds 1, 2, 3 and
// 7 run the cell balance (slot 77) on each cell's occupants, kinds 4, 5, 6,
// 9 and 10 the cell spawn (slot 78) with the kind's rate (slot 75), kind 8
// the lot sweep (slot 83). The cell balance keeps each class's density: the
// kinds' matrices (slot 72) summed, a tuning record per occupant key (the
// map 0x00B21D60, slot 91's key) and its curve, then the cell cull (slot 81)
// or one spawn (slot 80) at a cell drawn by weight. The cell spawn ages the
// cell's 4x4 kind table (the cells object's slots 0x54 and 0x60) and spawns
// (slot 79) on its open sides. The spawns (79, 80, 89) build the automaton's
// key from the class, a drawn lane and the sides, check the cell's tables,
// keep away from busy cells and from the camera's automaton (80), create it
// (slot 15, the game's) at the network's point (slot 25 0x0050BE90), give it
// its kind (0x0054E690) and its occupant (automaton slot 0x20C) or remove it
// again. The records' update (slot 84) spends each record of the +0xAA8
// vector (made by slot 70): every second, the network points near its
// object (the cells object's slot 0x68) get automata by Dispatch (slot 18)
// or slot 71, until the record's count is spent or five tries failed.
// GetRandomCreationPoint (slot 16) draws a point of the network near a
// position (the cells object's slot 0x64 within a radius clamped to the
// city's size); with flags 0x22000 it is the common base's 0x00506DC0. The
// Dispatch pair finds a path (+0xA88's slot 0x18 path object) for an
// automaton (17) or creates one at the start (18).
//
// Oddities kept: slot 52 reads the occupant's rectangle only when it has
// one (ours starts it at zero; see stack leftovers) and calls the common
// base's stub; the pedestrian manager's slot 53 never calls the base's;
// GetRandomCreationPoint passes 0 for the common base's seventh argument
// and leaks the network points when the cells object answers false;
// slot 17 leaks the path object when the automaton is not an occupant's and
// answers the automaton even when slot 19 fails; slot 89 leaves the
// automaton made when it is not an occupant's; the record list's update
// tests the object it has just cleared; slot 84 does not release the points
// when the cells object answers false; slot 76 sends the first kind's clock
// to slot 77, whatever the kind; slot 72's sign flag keeps the upper bytes
// of its first argument.
//
// Stack leftovers the game would read: slot 52's rectangle without an
// occupant (ours 0); the four floats of a new record of the tuning map
// (0x00555400, a key the tuning did not load; ours 0); the cell balance's
// records past the last when the drawn weight is not spent (ours stops
// there); the upper bytes of the byte loop counters slot 78 passes as
// dwords (ours 0). Each has a first-call line of its own; none was logged
// in the runs.
//
// Floats as in parts v1 and v2: 53-bit doubles, floats stored where the
// game stores them; FST keeps the unrounded value on the x87 stack and ours
// keeps the double the same way (slot 76's kind value, slot 77's row sums,
// slot 80's distance). The cell of a position is the game's floor: x
// times 1/16, less 0.5, plus 1.5 x 2^23 rounded to a float, its bits less
// 0x4B400000, plus one when that is the scaled value. The ftol conversions
// of GetRandomCreationPoint truncate.
//
// Return values: EAX's top bytes follow the game's where they come from a
// call or a pointer (slots 52, 53 vehicle, 55, 63, 69, 71, the Dispatch
// pair, 16 with flags 0x22000); the others answer AL alone (0 or 1 above
// it), and slots 56, 72, 76, 77, 78, 80, 81, 84's helper, 0x00552BD0 and
// 0x0054E690 answer nothing (their callers read nothing).
//
// Kept as the game's, called at their addresses or through the vtable: the
// common base's CreateAutomaton (slot 15, 0x00505370: SC4UIScale's probe
// site), slot 46 (0x00510690, which makes none when +0x4C is clear and the
// caller's byte is 0), slots 19, 20 and 25 (0x00507120, 0x00506F50,
// 0x0050BE90), 0x00506DC0 and its slots 52, 55, 56 and 63 (0x004FC900,
// 0x0050C7E0, 0x0050F2C0, 0x0050A7B0); the base's jump 0x0054D250 and the
// record's rectangles 0x0054EC60; the cells object, the path object and
// the automata themselves. No NAM site is among the 29; none holds an x87
// value across a call other than the C runtime's ftol, which C++'s
// truncation is. Every vtable call's argument count was checked against
// the callee's ret where the callee is known (the managers' and the road
// automata's vtables) and against the stack offsets after it elsewhere.
// determinism.sim_timers keys the record list's stopwatch (+0xAC0) on its
// reader's address (0x00552BD0's range); our update claims it before it
// reads (sim_timers::Claim). Found by the parity batch, as was slot 72's
// second factor (the sign's absolute value, not the scale's).

constexpr std::uintptr_t kCellScale = 0x00A9BCF4;      // float 1/16
constexpr std::uintptr_t kHalf = 0x00A84D2C;           // float 0.5
constexpr std::uintptr_t kRound23 = 0x00A9BCF8;        // float 1.5 x 2^23
constexpr std::uintptr_t kThousandth = 0x00A867A4;     // float 0.001
constexpr std::uintptr_t kOne = 0x00A81228;            // float 1.0
constexpr std::uintptr_t kHundredth = 0x00A823A4;      // float 0.01
constexpr std::uintptr_t kHundredthDouble = 0x00A9C328; // double 0.01
constexpr std::uintptr_t kZeroDouble = 0x00A80990;     // double 0.0
constexpr std::uintptr_t kTwoPow32 = 0x00A80AA8;       // float 2^32
constexpr std::uintptr_t kBudget = 0x00A9BCE4;         // float 5.0 (ms)
constexpr std::uintptr_t kCullSpeed = 0x00A9BEF8;      // float 0.95
constexpr std::uintptr_t kSpawnLimit = 0x00A956F0;     // double
constexpr std::uintptr_t kNeighbours = 0x00A9BD10;     // 8 (dx, dz) pairs
constexpr std::uintptr_t kClassTable = 0x00A9C330;     // int[3]
constexpr std::uintptr_t kTuningMap = 0x00B21D60;      // the cell balance's records by key
constexpr std::uintptr_t kKindMask = 0x00B21D5C;       // uint32, tuning
constexpr std::uintptr_t kRecordSpawn = 0x00B21CC4;    // float, tuning
constexpr std::uintptr_t kServiceCount = 0x00B21D14;   // uint32, tuning
constexpr std::uintptr_t kServiceBounds = 0x00B21D06;  // 4 bytes, tuning
constexpr std::uintptr_t kSpawnFactor = 0x00B21D00;    // float, tuning
constexpr std::uintptr_t kKind2Factor = 0x00B21D40;    // float, tuning
constexpr std::uintptr_t kQuietTarget = 0x00B21D38;    // float, tuning
constexpr std::uintptr_t kSpeed = 0x00B21C84;          // float, tuning
constexpr std::uintptr_t kLaneLow = 0x00B21BD4, kLaneHigh = 0x00B21BD8; // uint32, tuning
constexpr std::uintptr_t kPaused = 0x00B21B9C;         // byte
constexpr std::uintptr_t kNoPosition = 0x00B2293C;     // float, filled at startup
constexpr std::uintptr_t kAutomataService = 0x00B43D70; // the lot sweep's records
constexpr std::uintptr_t kDayNight = 0x00B43DC4;       // slot 0x58: 1 day, 2 night
constexpr std::uintptr_t kDensities = 0x00B43DA8;      // slot 0x5C: a class's target

// Part v3's slots (bytes).
namespace v3 {
inline constexpr std::size_t kOccupantPosition = 0x64;  // 25, the common base's (0x0050BE90)
inline constexpr std::size_t kCreateAutomaton = 0x3C;   // 15, the common base's (0x00505370)
inline constexpr std::size_t kDispatchNew = 0x48;       // 18
inline constexpr std::size_t kPathEnds = 0x50;          // 20, the common base's (0x00506F50)
inline constexpr std::size_t kPlace = 0x4C;             // 19, the common base's (0x00507120)
inline constexpr std::size_t kSlot13 = 0x34, kSlot27 = 0x6C, kSlot30 = 0x78, kSlot42 = 0xA8, kSlot46 = 0xB8;
inline constexpr std::size_t kSlot64 = 0x100, kAddRecord = 0x118, kCreateFor = 0x11C, kKindMatrix = 0x120;
inline constexpr std::size_t kKindSweep = 0x130, kCellBalance = 0x134, kCellSpawn = 0x138, kSpawnAt = 0x13C;
inline constexpr std::size_t kSpawnNear = 0x140, kCellCull = 0x144, kLotSweep = 0x14C, kUpdateRecords = 0x150;
inline constexpr std::size_t kSpawnFor = 0x164, kKey = 0x16C;
} // namespace v3

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

float BitsFloat(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// The game's floor of a scaled value: (value - 0.5 + 1.5 x 2^23) rounded to
// a float, its bits less 0x4B400000; one more when that is the value (a NaN
// keeps the first).
std::int32_t Floor(double value)
{
    const float rounded =
        static_cast<float>((value - static_cast<double>(Global<float>(kHalf))) + static_cast<double>(Global<float>(kRound23)));
    const std::uint32_t low = FloatBits(rounded) + 0xB4C00000u;
    const std::int32_t next = static_cast<std::int32_t>(low + 1);
    return static_cast<double>(next) == value ? next : static_cast<std::int32_t>(low);
}

// A position's cell: its coordinate times 1/16, the game's floor.
std::int32_t CellOf(float coordinate)
{
    return Floor(static_cast<double>(coordinate) * static_cast<double>(Global<float>(kCellScale)));
}

// MSVC's _ftol2: truncation, the low dword of the 64-bit answer.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value));
}

// An int rectangle (x0, z0, x1, z1) from an occupant's slot 0xC0 part (its
// slot 0x2C).
void OccupantRect(void* occupant, std::int32_t* rect)
{
    void* const part = Virtual<void*>(occupant, 0xC0);
    Virtual<std::uint32_t>(part, 0x2C, rect);
}

void Release(void* object) { Virtual<std::uint32_t>(object, slot::kRelease); }
void AddRef(void* object) { Virtual<std::uint32_t>(object, slot::kAddRef); }

// The pattern of a referenced field set from a call: the new one referenced,
// stored, the old one released, when they differ.
void SetReferenced(char* object, std::size_t offset, void* now)
{
    void* const old = At<void*>(object, offset);
    if (now == old)
        return;
    if (now)
        AddRef(now);
    At<void*>(object, offset) = now;
    if (old)
        Release(old);
}

bool OccupantTest(char* self, void* automaton)
{
    return CallThis<std::uint8_t>(addr::cSC4VehPedManagerBase_IsAutomatonOccupant, self, automaton) != 0;
}

double Unit(char* self) { return CallThis<double>(addr::SC4Random_Unit, self + 0x50); }
std::uint32_t Below(char* self, std::uint32_t n) { return CallThis<std::uint32_t>(addr::SC4Random_Below, self + 0x50, n); }

// A vector of pointers on the stack, as the game's (begin, end, capacity).
struct PointerVector {
    void** begin = nullptr;
    void** end = nullptr;
    void** capacity = nullptr;
};

// ------------------------------------------------------------------------------------
// Creation and dispatch

// 0x00553080, slot 16 GetRandomCreationPoint (group, position, the least
// and the most distance, out, flags, g), ret 0x1C.
std::uint32_t __fastcall GetRandomCreationPoint(char* self, void*, std::uint32_t group, const float* position,
                                                std::uint32_t least, std::uint32_t most, float* out,
                                                std::uint32_t flags, std::uint32_t g)
{
    static bool logged;
    FirstCall("GetRandomCreationPoint", logged);
    (void)g;
    if (flags & 0x22000) {
        static bool common;
        FirstCall("GetRandomCreationPoint the common base's", common);
        return CallThis<std::uint32_t>(addr::cSC4AutomataManager_GetRandomCreationPoint, self, group, position, least,
                                       most, out, flags, 0u);
    }
    const std::uint32_t allowed = Virtual<std::uint32_t>(self, v3::kSlot30, group);
    if (!(allowed & 0xFF))
        return allowed & 0xFFFFFF00u;
    std::uint8_t made = 0;
    std::uint32_t cellFlags;
    if (group == 0x4208 || group == 0x4217)
        cellFlags = 0x80;
    else if (CallThis<std::uint8_t>(addr::cSC4VehPedManagerBase_IsGroup422C, self, group))
        cellFlags = 0x100;
    else if (CallThis<std::uint8_t>(addr::cSC4VehPedManagerBase_IsGroup422E, self, group))
        cellFlags = 0x200;
    else
        cellFlags = CallThis<std::uint8_t>(addr::cSC4VehPedManagerBase_IsServiceGroup, self, group) ? 2u : 0xC49u;
    // The cell of the position, then the lot's rectangle there or the cell.
    std::int32_t cellX = 0, cellZ = 0;
    Virtual<std::uint32_t>(City(), 0x17C, FloatBits(position[0]), FloatBits(position[2]), &cellX, &cellZ);
    std::int32_t rect[4];
    void* lot = nullptr;
    if (void* const lots = Global<void*>(addr::SC4_LotManager))
        lot = Virtual<void*>(lots, 0x18, cellX, cellZ, 0u);
    if (lot)
        Virtual<std::uint32_t>(lot, 0x38, rect);
    else {
        rect[0] = rect[2] = cellX;
        rect[1] = rect[3] = cellZ;
    }
    // The most distance clamped to the least and to the city's size (cells
    // times the cell's size).
    void* const city = City();
    const float cells = static_cast<float>(Virtual<std::int32_t>(city, 0x174));
    const float size = static_cast<float>(Virtual<double>(city, 0x16C) * static_cast<double>(cells));
    const float lo = BitsFloat(least), hi = BitsFloat(most);
    float radius;
    if (hi < lo)
        radius = lo;
    else if (hi > size)
        radius = size;
    else
        radius = hi;
    const double scale = static_cast<double>(Global<float>(kCellScale));
    const std::int32_t inner = Truncate(static_cast<double>(lo) * scale);
    const std::int32_t outer = Truncate(static_cast<double>(radius) * scale);
    PointerVector points;
    void* const cellsObject = At<void*>(self, field::kCells);
    if (Virtual<std::uint8_t>(cellsObject, 0x64, &points, rect, outer, inner, cellFlags)) {
        const std::uint32_t index = Below(self, static_cast<std::uint32_t>(points.end - points.begin));
        void* const point = points.begin[index];
        if (point) {
            std::int32_t a = 0, b = 0; // the game writes them into its arguments' slots
            Virtual<std::uint32_t>(point, 0xEC, &b, &a);
            Virtual<std::uint32_t>(City(), 0x184, b, a, &out[0], &out[2]);
            out[1] = static_cast<float>(Virtual<double>(point, 0xE0, FloatBits(out[0]), FloatBits(out[2])));
            made = 1;
        }
        for (void** p = points.begin; p != points.end; ++p)
            Release(*p);
        points.end = points.begin;
    }
    if (points.begin)
        Free(points.begin);
    return made;
}

// 0x00551730, slot 17: Dispatch an automaton from a place to a place
// (automaton, from, to, b, least), ret 0x14. The answer is the automaton
// when its path is set, 0 otherwise.
void* __fastcall DispatchAutomaton(char* self, void*, char* automaton, std::uint32_t from, std::uint32_t to,
                                   std::uint32_t b, std::uint32_t least)
{
    static bool logged;
    FirstCall("Dispatch (slot 17)", logged);
    if (!automaton)
        return nullptr;
    std::int32_t start[4];
    if (!Virtual<std::uint8_t>(self, v3::kPathEnds, from, start)) {
        // From the automaton's own cell, inside the city.
        float position[3];
        Virtual<std::uint32_t>(automaton, 0x1C, position);
        void* const city = City();
        if (!city)
            return nullptr;
        const std::int32_t x = CellOf(position[0]);
        const std::int32_t z = CellOf(position[2]);
        start[0] = start[2] = x;
        start[1] = start[3] = z;
        if (x < 0 || x >= Virtual<std::int32_t>(city, 0x174) || z < 0 || z >= Virtual<std::int32_t>(city, 0x178))
            return nullptr;
    }
    std::int32_t end[4];
    if (!Virtual<std::uint8_t>(self, v3::kPathEnds, to, end))
        return nullptr;
    void* path = nullptr;
    Virtual<std::uint32_t>(At<void*>(self, field::kA88), 0x18, &path);
    if (!path)
        return nullptr;
    if (!OccupantTest(self, automaton)) {
        static bool leak;
        FirstCall("Dispatch (slot 17) with an automaton not an occupant's", leak);
        return nullptr; // the path object is not released, as in the game
    }
    Virtual<std::uint32_t>(automaton, 0x78, b, 1u);
    const std::uint32_t mode = 1u << (At<std::uint32_t>(self, field::kA90) & 31);
    struct Rect {
        std::int32_t v[4];
    };
    Rect startRect{{start[0], start[1], start[2], start[3]}};
    Rect endRect{{end[0], end[1], end[2], end[3]}};
    Virtual<std::uint32_t>(path, 0x4C, startRect);
    Virtual<std::uint32_t>(path, 0x34, mode, 1u);
    Virtual<std::uint32_t>(path, 0x48, 0u);
    Virtual<std::uint32_t>(path, 0x14, 1u);
    Virtual<std::uint32_t>(path, 0x14, 2u);
    Virtual<std::uint32_t>(path, 0x14, 0x10u);
    Virtual<std::uint32_t>(path, 0x24, 3u);
    void* const cellsOfPath = automaton + 0x22C;
    Virtual<std::uint32_t>(path, 0x20, cellsOfPath);
    Virtual<std::uint32_t>(path, 0x50, endRect);
    Virtual<std::uint32_t>(automaton, 0x78, 4u, 0u);
    void* answer = nullptr;
    if (Virtual<std::uint32_t>(path, 0x5C) == 1 &&
        CallThis<std::uint32_t>(addr::SC4CellDeque_Size, cellsOfPath) >= (least & 0xFF) &&
        Virtual<std::uint8_t>(automaton, 0x238, 0u)) {
        Virtual<std::uint32_t>(automaton, 0x78, 0x40u, 1u);
        Virtual<std::uint32_t>(automaton, 0x78, 0x2000u, 0u);
        Virtual<std::uint32_t>(automaton, 0x78, 0x100u, 0u);
        Virtual<std::uint32_t>(automaton, 0x78, 0x200u, 0u);
        Virtual<std::uint32_t>(automaton, 0x78, 0x20u, 0u);
        Virtual<std::uint32_t>(automaton, 0x280);
        const std::uint32_t none = Global<std::uint32_t>(kNoPosition);
        std::uint32_t place[3] = {none, none, none};
        answer = automaton;
        if (Virtual<std::uint8_t>(self, v3::kPlace, to, place)) {
            const std::uint32_t* const pair = reinterpret_cast<const std::uint32_t*>(from);
            const std::uint32_t occupant = pair[0] ? 0u : pair[1];
            Virtual<std::uint32_t>(automaton, 0x140, place, occupant);
        }
    }
    Release(path);
    return answer;
}

// 0x0054DBF0, slot 18: Dispatch a new automaton (class, from, to, b, c,
// position, d), ret 0x1C. The answer is the automaton's path answer (slot
// 17's) or 0.
void* __fastcall DispatchNew(char* self, void*, std::uint32_t classId, std::uint32_t from, std::uint32_t to,
                             std::uint32_t b, std::uint32_t c, const float* position, std::uint32_t d)
{
    static bool logged;
    FirstCall("Dispatch (slot 18)", logged);
    std::uint32_t place[3] = {FloatBits(position[0]), FloatBits(position[1]), FloatBits(position[2])};
    char* point = nullptr;
    if (place[2] == 0) {
        std::int32_t rect[4] = {};
        if (Virtual<std::uint8_t>(self, v3::kPathEnds, from, rect)) {
            point = Virtual<char*>(self, v3::kOccupantPosition, classId, 0u, rect[0], rect[1]);
            if (point) {
                place[0] = At<std::uint32_t>(point, 0x10);
                place[1] = At<std::uint32_t>(point, 0x14);
                place[2] = At<std::uint32_t>(point, 0x18);
            }
        }
    }
    char* const automaton = Virtual<char*>(self, v3::kSlot46, classId, b, place, point, d);
    if (!automaton)
        return nullptr;
    Virtual<std::uint32_t>(self, slot::kAddAutomaton, automaton);
    char* const answer = Virtual<char*>(self, 0x44, automaton, from, to, b, c);
    Release(automaton);
    if (!answer) {
        Virtual<std::uint32_t>(self, slot::kRemoveAutomaton, automaton, 1u);
        return nullptr;
    }
    if (!At<std::uint8_t>(self, 0x4C) && !Virtual<std::uint8_t>(automaton, 0x7C, 0x80u))
        Virtual<std::uint32_t>(answer, 0x24, 0u);
    return answer;
}

// 0x005538D0, slot 70: a record on the +0xAA8 vector (a, object, count,
// byte, byte, flags), ret 0x18; AL 0 without the object.
std::uint32_t __fastcall AddRecord(char* self, void*, std::uint32_t a, void* object, std::uint32_t count,
                                   std::uint32_t byte1, std::uint32_t byte2, std::uint32_t flags)
{
    static bool logged;
    FirstCall("AddRecord", logged);
    if (!object)
        return 0;
    std::uint32_t zero[7] = {};
    char*& end = At<char*>(self, field::kRecordsEnd);
    if (end != At<char*>(self, field::kRecordsCapacity)) {
        if (end)
            std::memcpy(end, zero, sizeof zero);
        end += 0x1C;
    } else {
        std::uint32_t tag = 0;
        CallThis<void>(addr::SC4Record28Vector_InsertN, self + field::kRecordsBegin, end,
                       static_cast<const void*>(zero), static_cast<void*>(&tag), 1u, 1u);
    }
    char* const record = At<char*>(self, field::kRecordsEnd) - 0x1C;
    At<std::uint32_t>(record, 0) = a;
    At<void*>(record, 4) = object;
    AddRef(object);
    At<std::uint32_t>(record, 8) = count;
    At<std::uint32_t>(record, 0x10) = flags | 0x2000000u;
    At<std::uint32_t>(record, 0x18) = 0;
    At<std::uint32_t>(record, 0x14) = 0;
    At<std::uint8_t>(record, 0xC) = static_cast<std::uint8_t>(byte1);
    At<std::uint8_t>(record, 0xD) = static_cast<std::uint8_t>(byte2);
    return 1;
}

// 0x0054DD50 (group, occupant, b, out), ret 0x10: the network point the
// common base's slot 25 gives at the occupant's corner, its position to out.
void* __fastcall OccupantPosition(char* self, void*, std::uint32_t group, void* occupant, std::uint32_t b, float* out)
{
    static bool logged;
    FirstCall("OccupantPosition", logged);
    std::int32_t rect[4];
    OccupantRect(occupant, rect);
    char* const point = Virtual<char*>(self, v3::kOccupantPosition, group, b, rect[0], rect[1]);
    if (point) {
        At<std::uint32_t>(out, 0) = At<std::uint32_t>(point, 0x10);
        At<std::uint32_t>(out, 4) = At<std::uint32_t>(point, 0x14);
        At<std::uint32_t>(out, 8) = At<std::uint32_t>(point, 0x18);
    }
    return point;
}

// 0x0054E690, this = an automaton (kind), ret 4: its occupant (+0x1EC) let
// go and given again around the kind's store at +0x1E4 (slot 0x210).
void __fastcall SetAutomatonKind(char* automaton, void*, std::uint32_t kind)
{
    static bool logged;
    FirstCall("SetAutomatonKind", logged);
    void* const occupant = At<void*>(automaton, 0x1EC);
    if (occupant)
        AddRef(occupant);
    const std::uint32_t key = At<std::uint32_t>(automaton, 0x1F4);
    Virtual<std::uint32_t>(automaton, 0x210, 0u, key, 0u, 1u);
    At<std::uint32_t>(automaton, 0x1E4) = kind;
    Virtual<std::uint32_t>(automaton, 0x210, occupant, key, 0u, 1u);
    if (occupant)
        Release(occupant);
}

// 0x0054F8A0, slot 71: an automaton for an occupant (occupant, byte, group,
// b, near, kind), ret 0x18; the automaton or 0.
void* __fastcall CreateFor(char* self, void*, void* occupant, std::uint32_t byte, std::uint32_t group,
                           std::uint32_t b, std::uint32_t nearby, std::uint32_t kind)
{
    static bool logged;
    FirstCall("CreateFor", logged);
    if (!occupant)
        return nullptr;
    void* const lanes = Virtual<void*>(occupant, 0xC4);
    if (!lanes)
        return nullptr;
    std::uint32_t key = 0;
    Virtual<std::uint32_t>(lanes, 0x24, kind ? kind : At<std::uint32_t>(self, field::kDefaultClass), 0xFFu, 0xFFu,
                           self + 0x50, &key, 0u);
    std::uint32_t at[3] = {};
    if (char* const entry = Virtual<char*>(lanes, 0xC, key, 0u)) {
        const std::uint32_t* const p = At<std::uint32_t*>(entry, 0);
        at[0] = p[0];
        at[1] = p[1];
        at[2] = p[2];
    }
    float to[3] = {};
    CallThis<void*>(addr::cSC4VehPedManagerBase_OccupantPosition, self, group, occupant,
                    (nearby & 0xFF) ? 7u : 0u, static_cast<float*>(to));
    if (FloatBits(to[2]) == 0)
        return nullptr;
    if (group != 0x4002 && group != 0x4001)
        to[0] = to[1] = to[2] = 0.0f;
    char* const automaton = Virtual<char*>(self, v3::kCreateAutomaton, group, at, b, to, 0u, 0u, 0u);
    if (!automaton)
        return nullptr;
    if (OccupantTest(self, automaton)) {
        CallThis<void>(addr::cSC4VehPedManagerBase_SetAutomatonKind, automaton, 7u);
        if (Virtual<std::uint8_t>(automaton, 0x20C, occupant, key, 0u)) {
            At<std::uint8_t>(automaton, 0x1E8) = static_cast<std::uint8_t>(byte);
            return automaton;
        }
    }
    Virtual<std::uint32_t>(self, slot::kRemoveAutomaton, automaton, 1u);
    return nullptr;
}

// 0x0054DDB0, slot 72 (occupant, b, kind, sign, out), ret 0x14: the kind's
// 4x4 at the occupant's corner from +0xA88's slot 0x30 (the sign's flag),
// times the scale (slot 87) and the sign's absolute value; kinds 7 and 8 are
// slot 88's matrix, others zero.
void __fastcall KindMatrix(char* self, void*, void* occupant, std::uint32_t b, std::uint32_t kind, std::uint32_t sign,
                           float* out)
{
    static bool logged;
    FirstCall("KindMatrix", logged);
    std::uint32_t index;
    switch (kind) {
    case 1: index = At<std::uint32_t>(self, field::kA90); break;
    case 2: index = 2; break;
    case 3: index = 4; break;
    case 4: index = 3; break;
    case 5: index = 5; break;
    case 6: index = 6; break;
    case 7:
    case 8:
        Virtual<std::uint32_t>(self, slot::kScaledMatrix, occupant, b, sign, out);
        return;
    case 9: index = 7; break;
    case 10: index = 8; break;
    default:
        std::memset(out, 0, 16 * sizeof(float));
        return;
    }
    const float scale = static_cast<float>(Virtual<double>(self, slot::kGetScale, occupant, kind));
    std::int32_t rect[4];
    OccupantRect(occupant, rect);
    // The flag keeps the first argument's upper bytes (its stack slot).
    const std::uint32_t negative =
        (reinterpret_cast<std::uintptr_t>(occupant) & 0xFFFFFF00u) | (BitsFloat(sign) < 0.0f ? 1u : 0u);
    // The second factor is the fourth argument's absolute value (its stack
    // slot after the flag), not the scale's.
    const float size = std::fabs(BitsFloat(sign));
    const std::uint16_t* const words =
        Virtual<const std::uint16_t*>(At<void*>(self, field::kA88), 0x30, rect[0], rect[1], index, negative);
    for (int i = 0; i < 16; ++i)
        out[i] = static_cast<float>(static_cast<double>(words[i]) * static_cast<double>(scale) *
                                    static_cast<double>(size));
}

// The cells object's chain at a cell: true when an automaton there answers
// its slot 0x234 (the cells of the occupant's rectangle grown by two).
bool BusyAround(char* self, void* occupant)
{
    std::int32_t rect[4];
    OccupantRect(occupant, rect);
    for (std::int32_t x = rect[0] - 2; x <= rect[2] + 2; ++x)
        for (std::int32_t z = rect[1] - 2; z <= rect[3] + 2; ++z)
            for (char* cell = Virtual<char*>(At<void*>(self, field::kCells), 0x38, x, z); cell;
                 cell = At<char*>(cell, 0x54)) {
                char* const head = At<char*>(cell, 0x1C);
                for (char* node = At<char*>(head, 0); node != At<char*>(cell, 0x1C); node = At<char*>(node, 0))
                    if (Virtual<std::uint8_t>(At<void*>(node, 8), 0x234))
                        return true;
            }
    return false;
}

// The spawns' side and lane (0x0054EF00 and 0x0054F3C0): the side d the
// occupant's slot 0xD4 takes for the class (d, the side opposite b, or the
// first of the others), or 0xFF with the lane table's slot 0xC; then a lane
// drawn from slot 0x30's count. False when there is none.
bool SpawnKey(char* self, void* occupant, void* lanes, std::uint32_t classId, std::uint32_t& d, std::uint32_t b,
              std::uint32_t& key)
{
    if (!Virtual<std::uint8_t>(occupant, 0xD4, classId, d, b)) {
        const std::uint32_t opposite = (b & 0xFFFFFF00u) | (((b & 0xFF) + 2) & 3);
        if (Virtual<std::uint8_t>(occupant, 0xD4, classId, opposite, b))
            d = (d & 0xFFFFFF00u) | (opposite & 0xFF);
        else {
            std::uint32_t side = 0;
            for (; side < 4; ++side) {
                if (side == (b & 0xFF))
                    continue;
                if (Virtual<std::uint8_t>(occupant, 0xD4, classId, (d & 0xFFFFFF00u) | side, b))
                    break;
            }
            d = (d & 0xFFFFFF00u) | side;
            if (side == 4) {
                const std::uint32_t any = classId << 24 | 0xFF00u | (b & 0xFF);
                if (!Virtual<void*>(lanes, 0xC, any, 0u))
                    return false;
                d = (d & 0xFFFFFF00u) | 0xFF;
            }
        }
    }
    const std::uint32_t count = Virtual<std::uint32_t>(lanes, 0x30, classId, d, b) & 0xFF;
    std::uint32_t lane;
    if (count > 1)
        lane = Below(self, count) & 0xFF;
    else if (count == 0)
        return false;
    else
        lane = 0;
    key = (((classId << 8 | lane) << 8 | (d & 0xFF)) << 8) | (b & 0xFF);
    return true;
}

// 0x0054EF00, slot 79: a spawn at an occupant's side (occupant, unused,
// kind, add, d, side, double limit, rate), ret 0x24. AL 1 when the spawn went as
// far as the creation (also when slot 15 made none), 0 otherwise.
std::uint32_t __fastcall SpawnAt(char* self, void*, void* occupant, std::uint32_t, std::uint32_t kind,
                                 std::uint32_t add, std::uint32_t d, std::uint32_t side, double limit,
                                 std::uint32_t rate)
{
    static bool logged;
    FirstCall("SpawnAt", logged);
    void* const lanes = Virtual<void*>(occupant, 0xC4);
    if (!lanes)
        return 0;
    const std::uint32_t classId = Virtual<std::uint32_t>(self, slot::kKindClass, kind);
    const std::uint32_t group = Virtual<std::uint32_t>(self, slot::kKindGroup, kind);
    std::uint32_t key = 0;
    if (!SpawnKey(self, occupant, lanes, classId, d, side, key))
        return 0;
    void* const cellsObject = At<void*>(self, field::kCells);
    float* const level = Virtual<float*>(cellsObject, 0x4C, occupant, kind, classId, d, side);
    *level = static_cast<float>(static_cast<double>(BitsFloat(add)) + static_cast<double>(*level));
    if (*Virtual<const std::uint16_t*>(cellsObject, 0x5C, occupant, kind, key) != 0)
        return 0;
    // On when the level reaches the limit's share.
    const double share = Global<double>(kSpawnLimit) / limit;
    if (!(share < static_cast<double>(*level) || share == static_cast<double>(*level)))
        return 0;
    if ((kind == 6 || kind == 4 || kind == 5 || kind == 9 || kind == 10) && BusyAround(self, occupant))
        return 0;
    float at[3] = {};
    if (void* const lanes2 = Virtual<void*>(occupant, 0xC4))
        if (void* const entry = Virtual<void*>(lanes2, 0xC, key, 0u))
            std::memcpy(at, At<void*>(entry, 0), sizeof at);
    float to[3] = {};
    std::int32_t rect[4];
    OccupantRect(occupant, rect);
    if (char* const point = Virtual<char*>(self, v3::kOccupantPosition, group, kind, rect[0], rect[1]))
        std::memcpy(to, point + 0x10, sizeof to);
    if (FloatBits(to[2]) == 0)
        return 0;
    char* const automaton = Virtual<char*>(self, v3::kCreateAutomaton, group, at, 2u, to, 0u, 0u, 0u);
    if (!automaton)
        return 1;
    CallThis<void>(addr::cSC4VehPedManagerBase_SetAutomatonKind, automaton, kind);
    if (!Virtual<std::uint8_t>(automaton, 0x20C, occupant, key, 0u)) {
        Virtual<std::uint32_t>(self, slot::kRemoveAutomaton, automaton, 1u);
        return 0;
    }
    Virtual<std::uint32_t>(automaton, 0x84, 0u, 0u);
    Virtual<std::uint32_t>(automaton, 0x84, 0x3F800000u, Global<std::uint32_t>(kSpeed));
    if (kind == 7 || kind == 8)
        At<std::uint8_t>(automaton, 0x1E8) = static_cast<std::uint8_t>(CallThis<std::uint32_t>(
            addr::cRZRandomSuper_Between, self + 0x50, Global<std::uint32_t>(kLaneLow), Global<std::uint32_t>(kLaneHigh)));
    if (CallThis<std::uint8_t>(addr::cSC4VehPedManagerBase_IsServiceGroup, self, group)) {
        // The service automaton's count: the rate over the tuning's count
        // (unsigned), the game's floor, within the group's bounds.
        const std::uint32_t n = Global<std::uint32_t>(kServiceCount);
        const double over = static_cast<double>(BitsFloat(rate)) / static_cast<double>(n);
        const std::uint32_t value = static_cast<std::uint32_t>(Floor(over));
        const std::uint8_t* const bounds = reinterpret_cast<const std::uint8_t*>(kServiceBounds);
        const bool own = group == 0x4216 || group == 0x4230 || group == 0x4233;
        const std::uint32_t low = own ? bounds[0] : bounds[2];
        const std::uint32_t high = own ? bounds[1] : bounds[3];
        std::uint32_t count = value;
        if (value < low)
            count = low;
        else if (value > high)
            count = high;
        Virtual<std::uint32_t>(automaton, 0x30C, count);
    }
    return 1;
}

// 0x0054F3C0, slot 80: a spawn near an occupant's side (occupant, unused,
// kind, d, side, unused), ret 0x18; nothing when the camera's automaton is
// closer than twice its slot 0x44.
void __fastcall SpawnNear(char* self, void*, void* occupant, std::uint32_t, std::uint32_t kind, std::uint32_t d,
                          std::uint32_t side, std::uint32_t)
{
    static bool logged;
    FirstCall("SpawnNear", logged);
    void* const lanes = Virtual<void*>(occupant, 0xC4);
    if (!lanes)
        return;
    const std::uint32_t classId = Virtual<std::uint32_t>(self, slot::kKindClass, kind);
    const std::uint32_t group = Virtual<std::uint32_t>(self, slot::kKindGroup, kind);
    std::uint32_t key = 0;
    if (!SpawnKey(self, occupant, lanes, classId, d, side, key))
        return;
    if ((kind == 6 || kind == 4 || kind == 5 || kind == 9 || kind == 10) && BusyAround(self, occupant))
        return;
    float at[3] = {};
    if (void* const entry = Virtual<void*>(lanes, 0xC, key, 0u))
        std::memcpy(at, At<void*>(entry, 0), sizeof at);
    if (void* const followed = Global<void*>(kFollowed)) {
        const std::uint32_t none = Global<std::uint32_t>(kNoPosition);
        std::uint32_t there[3] = {none, none, none};
        Virtual<std::uint32_t>(followed, 0x1C, there);
        const float dx = static_cast<float>(static_cast<double>(BitsFloat(there[0])) - static_cast<double>(at[0]));
        const float dy = static_cast<float>(static_cast<double>(BitsFloat(there[1])) - static_cast<double>(at[1]));
        const double dz = static_cast<double>(BitsFloat(there[2])) - static_cast<double>(at[2]);
        const float dzStored = static_cast<float>(dz);
        double sum = dz * static_cast<double>(dzStored);
        sum += static_cast<double>(dy) * static_cast<double>(dy);
        sum += static_cast<double>(dx) * static_cast<double>(dx);
        const float distance = static_cast<float>(std::sqrt(sum));
        const double size = Virtual<double>(Global<void*>(kFollowed), 0x44);
        if (size + size > static_cast<double>(distance))
            return;
    }
    float to[3] = {};
    std::int32_t rect[4];
    OccupantRect(occupant, rect);
    if (char* const point = Virtual<char*>(self, v3::kOccupantPosition, group, kind, rect[0], rect[1]))
        std::memcpy(to, point + 0x10, sizeof to);
    if (FloatBits(to[2]) == 0)
        return;
    char* const automaton = Virtual<char*>(self, v3::kCreateAutomaton, group, at, 2u, to, 0u, 0u, 0u);
    if (!automaton)
        return;
    CallThis<void>(addr::cSC4VehPedManagerBase_SetAutomatonKind, automaton, kind);
    if (!Virtual<std::uint8_t>(automaton, 0x20C, occupant, key, 0u)) {
        Virtual<std::uint32_t>(self, slot::kRemoveAutomaton, automaton, 1u);
        return;
    }
    Virtual<std::uint32_t>(automaton, 0x84, 0u, 0u);
    Virtual<std::uint32_t>(automaton, 0x84, 0x3F800000u, Global<std::uint32_t>(kSpeed));
    if (kind == 7 || kind == 8)
        At<std::uint8_t>(automaton, 0x1E8) = static_cast<std::uint8_t>(CallThis<std::uint32_t>(
            addr::cRZRandomSuper_Between, self + 0x50, Global<std::uint32_t>(kLaneLow), Global<std::uint32_t>(kLaneHigh)));
}

// 0x0054FA10, slot 89: a lot's automaton (occupant, cell, rate, count,
// kind), ret 0x14. The cell's automata of kind 8 past the share are culled
// (their slot 0x34); below it one of group 0x4102 is made at the lane's
// position. AL 1 when made.
std::uint32_t __fastcall SpawnFor(char* self, void*, void* occupant, char* cell, std::uint32_t rate,
                                  std::uint32_t count, std::uint32_t kind)
{
    static bool logged;
    FirstCall("SpawnFor", logged);
    std::int32_t rect[4];
    OccupantRect(occupant, rect);
    double n = static_cast<double>(static_cast<std::int32_t>(count));
    if (static_cast<std::int32_t>(count) < 0)
        n += static_cast<double>(Global<float>(kTwoPow32));
    const float share = static_cast<float>(n * static_cast<double>(At<float>(self, field::kAA4)) *
                                           static_cast<double>(Global<float>(kSpawnFactor)) *
                                           static_cast<double>(BitsFloat(rate)));
    float seen = 0.0f;
    char* const head = At<char*>(cell, 0x1C);
    for (char* node = At<char*>(head, 0); node != At<char*>(cell, 0x1C); node = At<char*>(node, 0)) {
        char* const automaton = At<char*>(node, 8);
        if (At<std::uint32_t>(automaton, 0x1E4) != 8)
            continue;
        seen = static_cast<float>(static_cast<double>(seen) + static_cast<double>(Global<float>(kOne)));
        if (static_cast<double>(share) + static_cast<double>(Global<float>(kOne)) < static_cast<double>(seen))
            Virtual<std::uint32_t>(automaton, 0x34, 0u);
    }
    if (!(seen < share))
        return 0;
    void* const lanes = Virtual<void*>(occupant, 0xC4);
    if (!lanes)
        return 0;
    // The lane's key goes into the stack slot that held the count seen.
    std::uint32_t key = FloatBits(seen);
    void* const lane = Virtual<void*>(lanes, 0x24, At<std::uint32_t>(self, field::kDefaultClass), 0xFFu, 0xFFu,
                                      self + 0x50, &key, 0u);
    if (!lane)
        return 0;
    const std::uint32_t none = Global<std::uint32_t>(kNoPosition);
    std::uint32_t at[3] = {none, none, none};
    Virtual<std::uint32_t>(lanes, 0x5C, key, at);
    char* fit = Virtual<char*>(self, v3::kSlot27, At<std::uint32_t>(self, field::kGroup), kind, 8u);
    if (!fit)
        fit = Virtual<char*>(self, v3::kSlot27, At<std::uint32_t>(self, field::kGroup), 1u, 8u);
    if (!fit)
        return 0;
    char* const automaton = Virtual<char*>(self, v3::kCreateAutomaton, 0x4102u, at, 2u, fit + 0x10, 0u, 0u, 0u);
    if (!automaton)
        return 0;
    if (!OccupantTest(self, automaton)) {
        static bool kept;
        FirstCall("SpawnFor with an automaton not an occupant's", kept);
        return 0; // left in the list, as in the game
    }
    CallThis<void>(addr::cSC4VehPedManagerBase_SetAutomatonKind, automaton, 8u);
    Virtual<std::uint32_t>(automaton, 0x210, occupant, key, lane, 1u);
    return 1;
}

// ------------------------------------------------------------------------------------
// Frames

// 0x0054ED90, slot 52 (occupant), ret 4: the common base's (0x004FC900, a
// stub answering true), then the automata of the occupant let go (slots
// 0x210 and 0x21C) and those of flag 0x2000 (not 0x20000) whose cell is in
// its rectangle removed (slot 0x21C). AL the common base's; the rest of EAX
// the list head's (empty list) or the manager's.
std::uint32_t __fastcall OccupantRemoved(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("OccupantRemoved", logged);
    const std::uint32_t answer = CallThis<std::uint32_t>(addr::cSC4AutomataManager_Slot52, self, occupant) & 0xFF;
    std::int32_t rect[4] = {};
    if (occupant)
        OccupantRect(occupant, rect);
    else {
        static bool none;
        FirstCall("OccupantRemoved without an occupant (its rectangle is a stack leftover in the game)", none);
    }
    char* const head = At<char*>(self, field::kAutomata);
    char* node = At<char*>(head, 0);
    if (node == head)
        return (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(head)) & 0xFFFFFF00u) | answer;
    do {
        char* const automaton = At<char*>(node, 8);
        node = At<char*>(node, 0);
        if (At<void*>(automaton, 0x1EC) == occupant) {
            Virtual<std::uint32_t>(automaton, 0x210, 0u, 0u, 0u, 0u);
            Virtual<std::uint32_t>(automaton, 0x21C);
        } else if (Virtual<std::uint8_t>(automaton, 0x7C, 0x2000u) && !Virtual<std::uint8_t>(automaton, 0x7C, 0x20000u)) {
            const std::uint32_t none = Global<std::uint32_t>(kNoPosition);
            std::uint32_t position[3] = {none, none, none};
            Virtual<std::uint32_t>(automaton, 0x1C, position);
            const std::int32_t x = CellOf(BitsFloat(position[0]));
            const std::int32_t z = CellOf(BitsFloat(position[2]));
            if (x >= rect[0] && z >= rect[1] && x <= rect[2] && z <= rect[3])
                Virtual<std::uint32_t>(automaton, 0x21C);
        }
    } while (node != At<char*>(self, field::kAutomata));
    return (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self)) & 0xFFFFFF00u) | answer;
}

// The occupant's two property arrays (slot 53): the IDs (property a's slot
// 0x19C, its count slot 0x10) and the bytes (property b's slot 0x17C, its
// count slot 0x10).
struct OccupantTables {
    void* list = nullptr;
    const std::uint32_t* ids = nullptr;
    const std::uint8_t* bytes = nullptr;
    std::uint32_t byteCount = 0;
};

bool ReadOccupantTables(void* part, std::uint32_t a, std::uint32_t b, OccupantTables& t)
{
    void* const properties = Virtual<void*>(part, 0x18);
    void* const first = Virtual<void*>(properties, 0x24, a);
    void* const second = Virtual<void*>(properties, 0x24, b);
    if (!first)
        return false;
    t.list = Virtual<void*>(first, 0x14);
    t.ids = Virtual<const std::uint32_t*>(t.list, 0x19C);
    if (second) {
        void* const data = Virtual<void*>(second, 0x14);
        t.bytes = Virtual<const std::uint8_t*>(data, 0x17C);
        t.byteCount = Virtual<std::uint32_t>(data, 0x10);
    }
    return true;
}

// 0x0055B480, the vehicle manager's slot 53 (occupant), ret 4: a record
// (slot 70) per ID of the occupant's property 0xE9AEE3DA while there is
// room, the byte of 0xE9AEE3DB its count (1 without); unless it filled up
// after making some, by a draw up to the tuning's 0x00B21CC4, one record
// 0x4206 for an occupant of kind 1. Then the base's slot 53 (the jump
// 0x0054D250 to the common base's 0x0050C6A0), whose answer it gives.
std::uint32_t __fastcall VehicleOccupantAdded(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("vehicle OccupantAdded", logged);
    bool made = false;
    void* const part = Virtual<void*>(occupant, 0x0C);
    OccupantTables t;
    bool full = false;
    if (ReadOccupantTables(part, 0xE9AEE3DAu, 0xE9AEE3DBu, t)) {
        std::uint32_t i = 0;
        if (Virtual<std::uint32_t>(t.list, 0x10) != 0) {
            do {
                if (At<std::uint32_t>(self, field::kCount) >= At<std::uint32_t>(self, field::kMaxCount)) {
                    full = true;
                    break;
                }
                const std::uint32_t count = (t.bytes && i < t.byteCount) ? t.bytes[i] : 1u;
                made = true;
                void* const owner = Virtual<void*>(occupant, 0x0C);
                Virtual<std::uint32_t>(self, v3::kAddRecord, t.ids[i], owner, count, 1u, 5u, 2u);
                ++i;
            } while (i < Virtual<std::uint32_t>(t.list, 0x10));
            if (!full)
                return CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_Slot53, self, occupant);
        }
    }
    if (!(full && made)) {
        if (*Virtual<const std::int32_t*>(occupant, 0x2C) == 1 &&
            At<std::uint32_t>(self, field::kCount) < At<std::uint32_t>(self, field::kMaxCount)) {
            const double draw = Unit(self);
            if (static_cast<double>(Global<float>(kRecordSpawn)) >= draw)
                Virtual<std::uint32_t>(self, v3::kAddRecord, 0x4206u, part, 1u, 0u, 8u, 2u);
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_Slot53, self, occupant);
}

// 0x00544300, the pedestrian manager's slot 53 (occupant), ret 4: a record
// per ID of the occupant's property 0xE9AEE3DC while there is room, the
// byte of 0xE9AEE3DD its count; the base's slot 53 is not called. AL 1.
std::uint32_t __fastcall PedestrianOccupantAdded(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("pedestrian OccupantAdded", logged);
    OccupantTables t;
    if (ReadOccupantTables(Virtual<void*>(occupant, 0x0C), 0xE9AEE3DCu, 0xE9AEE3DDu, t)) {
        std::uint32_t i = 0;
        if (Virtual<std::uint32_t>(t.list, 0x10) != 0)
            do {
                if (At<std::uint32_t>(self, field::kCount) >= At<std::uint32_t>(self, field::kMaxCount))
                    break;
                const std::uint32_t count = (t.bytes && i < t.byteCount) ? t.bytes[i] : 1u;
                void* const owner = Virtual<void*>(occupant, 0x0C);
                Virtual<std::uint32_t>(self, v3::kAddRecord, t.ids[i], owner, count, 0u, 2u, 2u);
                ++i;
            } while (i < Virtual<std::uint32_t>(t.list, 0x10));
    }
    return 1;
}

// 0x0054DB50 (first, last, to, tag, 0), cdecl: copies the 0x1C-byte records
// [first, last) to to, front to back; the end of the copy.
char* __cdecl CopyRecords(const char* first, const char* last, char* to)
{
    static bool logged;
    FirstCall("CopyRecords", logged);
    std::int32_t n = static_cast<std::int32_t>(last - first) / 0x1C;
    if (n <= 0)
        return to;
    for (; n; --n, first += 0x1C, to += 0x1C)
        for (int i = 0; i < 7; ++i)
            reinterpret_cast<std::uint32_t*>(to)[i] = reinterpret_cast<const std::uint32_t*>(first)[i];
    return to;
}

// 0x005516C0, slot 55 (object), ret 4: the +0xAA8 vector's records of the
// object released and erased, then the common base's slot 55 (0x0050C7E0),
// whose answer it gives.
std::uint32_t __fastcall RemoveRecordsOf(char* self, void*, void* object)
{
    static bool logged;
    FirstCall("RemoveRecordsOf", logged);
    if (object) {
        char* record = At<char*>(self, field::kRecordsBegin);
        while (record != At<char*>(self, field::kRecordsEnd)) {
            if (At<void*>(record, 4) != object) {
                record += 0x1C;
                continue;
            }
            Release(object);
            char* const end = At<char*>(self, field::kRecordsEnd);
            if (record + 0x1C != end) {
                std::uint32_t tag = 0;
                CallCdecl<char*>(addr::cSC4VehPedManagerBase_CopyRecords, static_cast<const char*>(record + 0x1C),
                                 static_cast<const char*>(end), record, static_cast<void*>(&tag), 0u);
            }
            At<char*>(self, field::kRecordsEnd) -= 0x1C;
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_Slot55, self, object);
}

// 0x0054F7B0, slot 56: the common base's (0x0050F2C0), then when +0x10 is
// set, per automaton its occupant's slot 0xC4 part at +0x1F0 (its slot 0xC
// with +0x1F4 to +0x1F8) and, when its slot 0x258 answers, its slot 0x250
// at +0xB0.
void __fastcall RefreshOccupants(char* self, void*)
{
    static bool logged;
    FirstCall("RefreshOccupants", logged);
    CallThis<std::uint32_t>(addr::cSC4AutomataManager_Slot56, self);
    if (!At<std::uint8_t>(self, 0x10))
        return;
    char* const head = At<char*>(self, field::kAutomata);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kAutomata); node = At<char*>(node, 0)) {
        char* const automaton = At<char*>(node, 8);
        if (void* const occupant = At<void*>(automaton, 0x1EC)) {
            SetReferenced(automaton, 0x1F0, Virtual<void*>(occupant, 0xC4));
            if (void* const lanes = At<void*>(automaton, 0x1F0))
                At<std::uint32_t>(automaton, 0x1F8) =
                    Virtual<std::uint32_t>(lanes, 0xC, At<std::uint32_t>(automaton, 0x1F4), 0u);
        }
        if (Virtual<std::uint8_t>(automaton, 0x258))
            SetReferenced(automaton, 0xB0, Virtual<void*>(automaton, 0x250));
    }
}

// 0x00552BD0, this = the record list field (+0xAB8): at most every 500 ms
// (the stopwatch +0xAC0 in ms, +0xAD8 the next time, +0xADC the last), each
// record's time (+0x3C) less the time since; a record past it is given back
// (the controller manager's slot 0x38 when held), released and freed, the
// others follow their object's cell (its slot 0x20, the city's slot 0x17C;
// 0x0054EC60 their rectangles).
void __fastcall UpdateRecordList(char* field, void*)
{
    static bool logged;
    FirstCall("UpdateRecordList", logged);
    // determinism.sim_timers takes this stopwatch over by its reader's
    // address (this function's range); ours reads it from core.dll.
    sim_timers::Claim(field + 8);
    const std::uint32_t now = CallThis<std::uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, field + 8);
    if (now < At<std::uint32_t>(field, 0x20))
        return;
    At<std::uint32_t>(field, 0x20) = now + 500;
    const std::uint32_t since = now - At<std::uint32_t>(field, 0x24);
    char* const head = At<char*>(field, 0);
    char* next = At<char*>(head, 0);
    if (next != head)
        do {
            char* const node = next;
            next = At<char*>(node, 0);
            char* const r = node + 8;
            if (At<std::uint32_t>(node, record::kD) < since) {
                if (At<std::uint8_t>(node, record::kHeld) && At<void*>(node, record::kObject)) {
                    void* const controllers = Virtual<void*>(City(), 0xBC);
                    Virtual<std::uint32_t>(controllers, 0x38, At<void*>(node, record::kObject));
                    At<std::uint8_t>(node, record::kHeld) = 0;
                }
                if (void* const object = At<void*>(node, record::kObject)) {
                    At<void*>(node, record::kObject) = nullptr;
                    Release(object);
                }
                At<std::uint32_t>(node, record::kD) = 0;
                char* const before = At<char*>(node, 4);
                char* const after = At<char*>(node, 0);
                At<char*>(before, 0) = after;
                At<char*>(after, 4) = before;
                if (void* const object = At<void*>(node, record::kObject))
                    Release(object);
                Free(node);
            } else {
                At<std::uint32_t>(node, record::kD) -= since;
                if (void* const object = At<void*>(node, record::kObject)) {
                    float position[3];
                    Virtual<std::uint32_t>(object, 0x20, position);
                    std::int32_t a, b;
                    Virtual<std::uint32_t>(City(), 0x17C, FloatBits(position[0]), FloatBits(position[2]), &a, &b);
                    if (At<std::int32_t>(r, 0) != a || At<std::int32_t>(r, 4) != b) {
                        At<std::int32_t>(r, 4) = b;
                        At<std::int32_t>(r, 0) = a;
                        CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_RecordRect, field, r);
                    }
                }
            }
        } while (next != At<char*>(field, 0));
    At<std::uint32_t>(field, 0x24) = now;
}

// 0x00553300, slot 63 (a, time), ret 8: the record list's update, then
// while the manager may have automata the kind sweep (slot 76, with +0x4C)
// and the records' update (slot 84, with records); then the common base's
// slot 63 (0x0050A7B0), whose answer it gives.
std::uint32_t __fastcall Update(char* self, void*, std::uint32_t a, std::uint32_t time)
{
    static bool logged;
    FirstCall("Update", logged);
    CallThis<void>(addr::cSC4VehPedManagerBase_UpdateRecordList, self + field::kRecordList);
    if (At<std::int32_t>(self, field::kMaxCount) > 0 && !At<std::uint8_t>(self, 0x4D) && !Global<std::uint8_t>(kPaused)) {
        if (At<std::uint8_t>(self, 0x4C))
            Virtual<std::uint32_t>(self, v3::kKindSweep, time);
        if (At<char*>(self, field::kRecordsBegin) != At<char*>(self, field::kRecordsEnd))
            Virtual<std::uint32_t>(self, v3::kUpdateRecords, time);
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataManager_Slot63, self, a, time);
}

// 0x0055B600, the vehicle manager's slot 63 (a, time), ret 8: the base's,
// then slot 64 (time), whose answer it gives.
std::uint32_t __fastcall VehicleUpdate(char* self, void*, std::uint32_t a, std::uint32_t time)
{
    static bool logged;
    FirstCall("vehicle Update", logged);
    CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_Update, self, a, time);
    return Virtual<std::uint32_t>(self, v3::kSlot64, time);
}

// 0x0054FBC0, slot 69 (automaton), ret 4: the automata whose +0xB0 is it
// take their slot 0x250 there. EAX the list head (empty list) or the
// manager (RemoveAutomaton keeps its upper bytes).
std::uint32_t __fastcall ForgetAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("ForgetAutomaton", logged);
    char* const head = At<char*>(self, field::kAutomata);
    char* node = At<char*>(head, 0);
    if (node == head)
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(head));
    do {
        char* const other = At<char*>(node, 8);
        if (At<void*>(other, 0xB0) == automaton)
            SetReferenced(other, 0xB0, Virtual<void*>(other, 0x250));
        node = At<char*>(node, 0);
    } while (node != At<char*>(self, field::kAutomata));
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
}

// A stopwatch on the stack, as the game's (0x18 bytes, mode 4: ms).
struct alignas(8) LocalStopwatch {
    char bytes[0x18];
};

// 0x00552000, slot 76 (time), ret 4: the kind sweep (top of this part).
void __fastcall KindSweep(char* self, void*, std::uint32_t time)
{
    static bool logged;
    FirstCall("KindSweep", logged);
    std::uint8_t done = 0;
    void* const cellsObject = At<void*>(self, field::kCells);
    const std::int32_t* const area = Virtual<const std::int32_t*>(cellsObject, 0x44);
    char* const head = At<char*>(self, field::kKinds);
    if (At<char*>(head, 0) == head || !Global<std::uint32_t>(kKindMask))
        return;
    for (char* node = At<char*>(At<char*>(self, field::kKinds), 0); node != At<char*>(self, field::kKinds);
         node = At<char*>(node, 0))
        At<float>(node, 0xC) =
            static_cast<float>(static_cast<double>(BitsFloat(time)) + static_cast<double>(At<float>(node, 0xC)));
    std::int32_t& x = At<std::int32_t>(self, field::kA9C);
    std::int32_t& z = At<std::int32_t>(self, field::kAA0);
    std::uint32_t kind;
    if (x >= 0 && z >= 0) {
        kind = At<std::uint32_t>(At<char*>(self, field::kFirstKind), 8);
        if (x < area[0])
            x = area[0];
        if (z < area[1])
            z = area[1];
    } else {
        x = area[0];
        z = area[1];
        for (;;) {
            At<char*>(self, field::kFirstKind) = At<char*>(At<char*>(self, field::kFirstKind), 0);
            if (At<char*>(self, field::kFirstKind) == At<char*>(self, field::kKinds))
                At<char*>(self, field::kFirstKind) = At<char*>(At<char*>(self, field::kKinds), 0);
            kind = At<std::uint32_t>(At<char*>(self, field::kFirstKind), 8);
            char* const kinds = At<char*>(self, field::kKinds);
            if (At<char*>(kinds, 0) != kinds) {
                std::uint32_t count = 0;
                for (char* n = At<char*>(kinds, 0); n != kinds; n = At<char*>(n, 0))
                    ++count;
                if (count == 1)
                    break;
            }
            if ((1u << (kind & 31)) & Global<std::uint32_t>(kKindMask))
                break;
        }
    }
    const std::uint32_t light = Virtual<std::uint32_t>(Global<void*>(kDayNight), 0x58);
    if (kind >= 1 && kind <= 10 && light == (kind == 6 ? 2u : 1u)) {
        const double value = Virtual<double>(self, v3::kSlot42, kind);
        const float stored = static_cast<float>(value);
        if (!(std::fabs(value) < static_cast<double>(Global<float>(kThousandth)))) {
            if (kind == 8) {
                // The lot sweep: kept on when it ran out of time.
                if ((Virtual<std::uint32_t>(self, v3::kLotSweep, FloatBits(stored)) & 0xFF) == 0)
                    return;
            } else {
                const bool balance = kind <= 3 || kind == 7;
                LocalStopwatch watch;
                CallThis<void*>(addr::SC4Stopwatch_Construct, watch.bytes, 4u);
                CallThis<std::uint32_t>(addr::SC4Stopwatch_Start, watch.bytes);
                const double rate = balance ? 0.0 : Virtual<double>(self, slot::kKindRate, kind);
                const float budget = Global<float>(kBudget);
                if (z <= area[3])
                    while (!done) {
                        if (x <= area[2])
                            while (!done) {
                                for (char* cell = Virtual<char*>(cellsObject, 0x34, x, z); cell;
                                     cell = At<char*>(cell, 0x54)) {
                                    if (balance)
                                        Virtual<std::uint32_t>(
                                            self, v3::kCellBalance, At<void*>(cell, 0), cell, x, z,
                                            At<std::uint32_t>(At<char*>(At<char*>(self, field::kKinds), 0), 0xC));
                                    else
                                        Virtual<std::uint32_t>(self, v3::kCellSpawn, At<void*>(cell, 0), cell, kind, x,
                                                               z, FloatBits(stored), rate);
                                }
                                float elapsed = 0.0f;
                                CallThis<void>(addr::SC4Stopwatch_GetElapsed, watch.bytes, &elapsed);
                                if (!(elapsed < budget || elapsed != elapsed))
                                    done = 1;
                                ++x;
                                if (x > area[2])
                                    break;
                            }
                        if (x > area[2])
                            x = area[0];
                        ++z;
                        if (z > area[3])
                            break;
                    }
                if (!balance)
                    CallThis<void>(addr::SC4Stopwatch_Stop, watch.bytes);
                CallThis<void>(addr::SC4Stopwatch_Destruct, watch.bytes);
                if (done)
                    return;
            }
        }
    }
    x = -1;
    z = -1;
    At<std::uint32_t>(At<char*>(self, field::kFirstKind), 0xC) = 0;
}

// 0x00555350, this = the tuning map 0x00B21D60 (a pair: the key and 0x20
// bytes, four floats and an SC4Curve), ret 4: grown for one more (0x004F0940),
// a new 0x28-byte node at the head of the key's bucket; the pair in it.
char* __fastcall MapInsert(char* map, void*, const std::uint32_t* pair)
{
    static bool logged;
    FirstCall("MapInsert", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Resize, map, At<std::uint32_t>(map, 0x10) + 1);
    const std::uint32_t buckets =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(At<char*>(map, 8) - At<char*>(map, 4)) >> 2);
    const std::uint32_t bucket = pair[0] % buckets;
    char* const head = At<char**>(map, 4)[bucket];
    char* const node = static_cast<char*>(Allocate(0x28));
    At<char*>(node, 0) = nullptr;
    char* const value = node + 4;
    if (reinterpret_cast<std::uintptr_t>(value) != 0) {
        std::memcpy(value, pair, 0x14);
        CallThis<void*>(addr::SC4Curve_Copy, value + 0x14, static_cast<const void*>(pair + 5));
    }
    At<char*>(node, 0) = head;
    At<char**>(map, 4)[bucket] = node;
    ++At<std::uint32_t>(map, 0x10);
    return value;
}

// 0x00555400, this = the tuning map, ret 4: the record of the key (its four
// floats, then its curve), a new one when the key is not there.
char* __fastcall MapAt(char* map, void*, const std::uint32_t* key)
{
    static bool logged;
    FirstCall("MapAt", logged);
    const std::uint32_t buckets =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(At<char*>(map, 8) - At<char*>(map, 4)) >> 2);
    for (char* node = At<char**>(map, 4)[*key % buckets]; node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, 4) == *key)
            return node + 8;
    static bool added;
    FirstCall("MapAt with a new key (its four floats are stack leftovers in the game; ours 0)", added);
    std::uint32_t curve[4];
    CallThis<void*>(addr::SC4Curve_Init, static_cast<void*>(curve));
    std::uint32_t pair[9] = {*key, 0, 0, 0, 0};
    CallThis<void*>(addr::SC4Curve_Copy, static_cast<void*>(pair + 5), static_cast<const void*>(curve));
    char* const value = CallThis<char*>(addr::cSC4VehPedManagerBase_MapInsert, map, static_cast<const std::uint32_t*>(pair));
    if (pair[5])
        Free(reinterpret_cast<void*>(static_cast<std::uintptr_t>(pair[5])));
    if (curve[0])
        Free(reinterpret_cast<void*>(static_cast<std::uintptr_t>(curve[0])));
    return value + 4;
}

// A kind's record of the cell balance: the kind, its 4x4 (slot 72) and the
// sum, 0x48 bytes as the game keeps them on the stack.
struct KindRecord {
    std::uint32_t kind;
    float table[16];
    float sum;
};

// 0x005554E0, slot 77 (occupant, cell, x, z, time), ret 0x14: per class 1
// and 2 (0x00A9C330) the cell's density follows its target; the cell cull
// (slot 81) above it, a spawn (slot 80) at a drawn place below it.
void __fastcall CellBalance(char* self, void*, void* occupant, void* cell, std::int32_t x, std::int32_t z,
                            std::uint32_t time)
{
    static bool logged;
    FirstCall("CellBalance", logged);
    const std::uint32_t hits = CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_RecordListHits,
                                                       self + field::kRecordList, x, z);
    if (hits == 3)
        return;
    const float weight =
        static_cast<float>(static_cast<double>(BitsFloat(time)) * static_cast<double>(Global<float>(kThousandth)));
    void* const cellsObject = At<void*>(self, field::kCells);
    KindRecord records[12]; // the game's frame holds twelve; at most nine kinds come here
    for (std::uint32_t c = 1; c < 3; ++c) {
        const std::int32_t target = Global<std::int32_t>(kClassTable + 4 * c);
        if (target == -1)
            continue;
        const std::int32_t key = Virtual<std::int32_t>(self, v3::kKey, occupant, c);
        if (key == -1)
            continue;
        float* const density = Virtual<float*>(cellsObject, 0x50, occupant, c);
        const std::uint16_t now = *Virtual<const std::uint16_t*>(cellsObject, 0x58, occupant, c);
        *density = static_cast<float>(
            (static_cast<double>(now) * static_cast<double>(weight) + static_cast<double>(*density)) /
            (static_cast<double>(weight) + static_cast<double>(Global<float>(kOne))));
        std::int32_t n = 0;
        float sum = 0.0f;
        char* const head = At<char*>(self, field::kKinds);
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
            const std::uint32_t kind = At<std::uint32_t>(node, 8);
            if (kind == 8 || Virtual<std::uint32_t>(self, slot::kKindClass, kind) != c)
                continue;
            KindRecord& r = records[n++];
            r.kind = kind;
            float value = static_cast<float>(Virtual<double>(self, v3::kSlot42, kind));
            if (r.kind == 2)
                value = static_cast<float>(static_cast<double>(Global<float>(kKind2Factor)) * static_cast<double>(value));
            Virtual<std::uint32_t>(self, v3::kKindMatrix, occupant, cell, kind, FloatBits(value), r.table);
            r.sum = 0.0f;
            for (int row = 0; row < 4; ++row) {
                double total = static_cast<double>(r.table[4 * row]) + static_cast<double>(r.sum);
                total += static_cast<double>(r.table[4 * row + 1]);
                total += static_cast<double>(r.table[4 * row + 2]);
                total += static_cast<double>(r.table[4 * row + 3]);
                r.sum = static_cast<float>(total);
            }
            sum = static_cast<float>(static_cast<double>(sum) + static_cast<double>(r.sum));
            if (n == 12)
                break;
        }
        if (n <= 0 && !Virtual<std::uint8_t>(self, v3::kSlot13))
            continue;
        const float wanted =
            static_cast<float>(Virtual<double>(Global<void*>(kDensities), 0x5C, static_cast<std::uint32_t>(target), x, z));
        if (wanted == 0.0f)
            continue;
        std::uint32_t mapKey = static_cast<std::uint32_t>(key);
        char* const tuning = CallThis<char*>(addr::cSC4VehPedManagerBase_MapAt, reinterpret_cast<void*>(kTuningMap),
                                             static_cast<const std::uint32_t*>(&mapKey));
        float goal = static_cast<float>(CallThis<double>(
            addr::SC4Curve_Evaluate, tuning + 0x10, static_cast<float>(static_cast<double>(sum) / static_cast<double>(wanted))));
        if (Virtual<std::uint8_t>(self, v3::kSlot13))
            goal = Global<float>(kQuietTarget);
        const float current = *density;
        const double ratio = static_cast<double>(current) / static_cast<double>(goal);
        if (current > goal && !(hits & 2)) {
            // Too dense: the cull, past the record's margin, by its draw.
            if (!(goal == 0.0f) &&
                !(static_cast<double>(At<float>(tuning, 0xC)) + static_cast<double>(Global<float>(kOne)) < ratio))
                continue;
            if (!(static_cast<double>(At<float>(tuning, 8)) > Unit(self)))
                continue;
            Virtual<std::uint32_t>(self, v3::kCellCull, cell, c);
            continue;
        }
        if (!(goal > 0.0f) || !(sum > Global<float>(kHundredth)) || (hits & 1) ||
            !(ratio < static_cast<double>(At<float>(tuning, 4))))
            continue;
        if (Virtual<std::uint8_t>(cellsObject, 0x88, occupant))
            continue;
        if (!(static_cast<double>(At<float>(tuning, 0)) > Unit(self)))
            continue;
        if (n > 0) {
            // A kind by its sum, then a place of its 4x4 by its weight.
            float left = static_cast<float>(CallThis<double>(addr::cRZRandom_Range, self + 0x50,
                                                             Global<double>(kHundredthDouble), static_cast<double>(sum)));
            for (std::int32_t i = 0;; ++i) {
                if (i == n) {
                    static bool past;
                    FirstCall("CellBalance past the last record (stack leftovers in the game)", past);
                    break;
                }
                const KindRecord& r = records[i];
                const double rest = static_cast<double>(left) - static_cast<double>(r.sum);
                left = static_cast<float>(rest);
                if (rest < 0.0 || rest == 0.0) {
                    double place = CallThis<double>(addr::cRZRandom_Range, self + 0x50, Global<double>(kZeroDouble),
                                                    static_cast<double>(r.sum));
                    bool found = false;
                    for (std::uint32_t row = 0; row < 4 && !found; ++row)
                        for (std::uint32_t column = 0; column < 4; ++column) {
                            place -= static_cast<double>(r.table[4 * row + column]);
                            if (place < 0.0 || place == 0.0) {
                                Virtual<std::uint32_t>(self, v3::kSpawnNear, occupant, cell, r.kind, row, column,
                                                       FloatBits(r.table[4 * row + column]));
                                found = true;
                                break;
                            }
                        }
                    break;
                }
                if (!(left > 0.0f))
                    break;
            }
        } else if (Virtual<std::uint8_t>(self, v3::kSlot13)) {
            bool spawned = false;
            for (std::uint32_t row = 0; row < 4 && !spawned; ++row)
                for (std::uint32_t column = 0; column < 4; ++column)
                    if (Virtual<std::uint8_t>(occupant, 0xD4, c, row, column)) {
                        Virtual<std::uint32_t>(self, v3::kSpawnNear, occupant, cell, 1u, row, column, 0u);
                        spawned = true;
                        break;
                    }
        }
    }
}

// 0x00551A50, slot 78 (occupant, cell, kind, x, z, value, double rate), ret
// 0x20: the cell's 4x4 of the kind (the cells object's slots 0x54 and 0x60)
// follows its count by the kind's clock; a spawn (slot 79) on each place of
// the kind's matrix (slot 72) above 0 where the occupant has no side or the
// neighbour cell is open (slot 82).
void __fastcall CellSpawn(char* self, void*, void* occupant, void* cell, std::uint32_t kind, std::int32_t x,
                          std::int32_t z, std::uint32_t value, double rate)
{
    static bool logged;
    FirstCall("CellSpawn", logged);
    if (CallThis<std::uint32_t>(addr::cSC4VehPedManagerBase_RecordListHits, self + field::kRecordList, x, z) & 1)
        return;
    const float most = static_cast<float>(Virtual<double>(self, slot::kGetKindValue, kind));
    const std::uint32_t classId = Virtual<std::uint32_t>(self, slot::kKindClass, kind);
    const float clock = static_cast<float>(static_cast<double>(At<float>(At<char*>(self, field::kFirstKind), 0xC)) *
                                           static_cast<double>(Global<float>(kThousandth)));
    float table[16];
    Virtual<std::uint32_t>(self, v3::kKindMatrix, occupant, cell, kind, value, table);
    const float divisor = static_cast<float>(static_cast<double>(clock) + static_cast<double>(Global<float>(kOne)));
    void* const cellsObject = At<void*>(self, field::kCells);
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint8_t open = Virtual<std::uint8_t>(self, slot::kTestNeighbourCell, cell, x, z, i);
        for (std::uint32_t j = 0; j < 4; ++j) {
            float* const level = Virtual<float*>(cellsObject, 0x54, occupant, kind, classId, i, j);
            const std::uint16_t count = *Virtual<const std::uint16_t*>(cellsObject, 0x60, occupant, kind, classId, i, j);
            *level = static_cast<float>((static_cast<double>(count) * static_cast<double>(clock) +
                                         static_cast<double>(*level)) /
                                        static_cast<double>(divisor));
            const float place = table[4 * i + j];
            if (!(place > 0.0f))
                continue;
            if (Virtual<std::uint8_t>(occupant, 0xD4, classId, i, j) && !open)
                continue;
            double limit = static_cast<double>(place) * rate;
            if (limit > static_cast<double>(most))
                limit = static_cast<double>(most);
            Virtual<std::uint32_t>(self, v3::kSpawnAt, occupant, cell, kind,
                                   At<std::uint32_t>(At<char*>(self, field::kFirstKind), 0xC), i, j, limit,
                                   FloatBits(place));
        }
    }
}

// 0x005539A0, slot 81 (cell, class), ret 8: one of the cell's automata of
// the class, with flag 2 and none of 0x200000, 0x80 and 0x2000000, faster
// than 0.95 (slot 0x80), drawn and told to go (slot 0x34 with 0).
void __fastcall CellCull(char* self, void*, char* cell, std::uint32_t classId)
{
    static bool logged;
    FirstCall("CellCull", logged);
    PointerVector picks;
    char* const head = At<char*>(cell, 0x1C);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* automaton = At<void*>(node, 8);
        if (((Virtual<std::uint32_t>(automaton, 0x220) >> 24) & 0xF) != classId)
            continue;
        if (!Virtual<std::uint8_t>(automaton, 0x7C, 2u) || Virtual<std::uint8_t>(automaton, 0x7C, 0x200000u) ||
            Virtual<std::uint8_t>(automaton, 0x7C, 0x80u) || Virtual<std::uint8_t>(automaton, 0x7C, 0x2000000u))
            continue;
        if (!(Virtual<double>(automaton, 0x80) > static_cast<double>(Global<float>(kCullSpeed))))
            continue;
        CallThis<void>(addr::SC4PtrVector_PushBack, static_cast<void*>(&picks), static_cast<void*>(&automaton));
    }
    if (picks.begin != picks.end) {
        void* const pick = picks.begin[Below(self, static_cast<std::uint32_t>(picks.end - picks.begin))];
        Virtual<std::uint32_t>(pick, 0x34, 0u);
    }
    if (picks.begin)
        Free(picks.begin);
}

// 0x00553370, slot 83 (value), ret 4: the lot sweep. For the records of
// 0x00B43D70 (its slot 0x2C: 0x1C-byte records, cell x and z at +0 and +4,
// +8 passed on, +0xC a byte) inside the cells object's rectangle, the
// automata cell there or at one of eight neighbours (0x00A9BD10) gets slot
// 89 (its occupant, the cell, the value, how many records share the cell,
// +8); the service's slot 0x30 (index, 1) when it made one. At most 5 ms
// (its own stopwatch); AL 1 when it went through, 0 when it ran out.
std::uint32_t __fastcall LotSweep(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("LotSweep", logged);
    std::uint8_t ranOut = 0;
    LocalStopwatch watch;
    CallThis<void*>(addr::SC4Stopwatch_Construct, watch.bytes, 4u);
    float elapsed = 0.0f;
    if (void* const service = Global<void*>(kAutomataService)) {
        void* const cellsObject = At<void*>(self, field::kCells);
        const std::int32_t* const area = Virtual<const std::int32_t*>(cellsObject, 0x44);
        struct {
            char* begin = nullptr;
            char* end = nullptr;
            char* capacity = nullptr;
        } records;
        if (Virtual<std::uint8_t>(service, 0x2C, &records)) {
            CallThis<std::uint32_t>(addr::SC4Stopwatch_Start, watch.bytes);
            const std::uint32_t n = static_cast<std::uint32_t>(static_cast<std::int32_t>(records.end - records.begin) / 0x1C);
            const std::int32_t* const offsets = reinterpret_cast<const std::int32_t*>(kNeighbours);
            for (std::uint32_t i = 0; n;) {
                const char* const r = records.begin + 0x1C * i;
                const std::int32_t rx = At<std::int32_t>(const_cast<char*>(r), 0);
                const std::int32_t rz = At<std::int32_t>(const_cast<char*>(r), 4);
                if (!r[0xC] && rx >= area[0] && rz >= area[1] && rx <= area[2] && rz <= area[3]) {
                    char* lotCell = Virtual<char*>(cellsObject, 0x38, rx, rz);
                    for (std::uint32_t k = 0; !lotCell && k < 8; ++k)
                        lotCell = Virtual<char*>(cellsObject, 0x38, offsets[2 * k] + rx, offsets[2 * k + 1] + rz);
                    if (lotCell) {
                        std::uint32_t same = 0;
                        for (const char* o = records.begin; o != records.end; o += 0x1C)
                            if (At<std::int32_t>(const_cast<char*>(o), 0) == rx &&
                                At<std::int32_t>(const_cast<char*>(o), 4) == rz)
                                ++same;
                        if (Virtual<std::uint8_t>(self, v3::kSpawnFor, At<void*>(lotCell, 0), lotCell, value, same,
                                                  At<std::uint32_t>(const_cast<char*>(r), 8)))
                            Virtual<std::uint32_t>(service, 0x30, i, 1u);
                    }
                }
                CallThis<void>(addr::SC4Stopwatch_GetElapsed, watch.bytes, &elapsed);
                if (!(elapsed < Global<float>(kBudget) || elapsed != elapsed)) {
                    ranOut = 1;
                    break;
                }
                if (++i >= n)
                    break;
            }
        }
        if (records.begin)
            Free(records.begin);
    }
    CallThis<void>(addr::SC4Stopwatch_Destruct, watch.bytes);
    return ranOut ? 0u : 1u;
}

// 0x00552DE0, slot 84 (time), ret 4: the +0xAA8 vector's records (a, object,
// count, two bytes, flags, a timer and tries; slot 70). A record whose timer
// is spent takes the network points near its object (the cells object's
// slot 0x68) and makes automata at drawn ones, slot 71 for the first byte
// set, else Dispatch (slot 18) from the point's occupant to the object;
// each point that took one is let go. The record goes when its count is
// spent or after five tries, else waits 1000 again. AL 1.
std::uint32_t __fastcall UpdateRecords(char* self, void*, std::uint32_t time)
{
    static bool logged;
    FirstCall("UpdateRecords", logged);
    char* rec = At<char*>(self, field::kRecordsBegin);
    if (rec == At<char*>(self, field::kRecordsEnd))
        return 1;
    char* next = rec + 0x1C;
    void* const cellsObject = At<void*>(self, field::kCells);
    do {
        const double left = static_cast<double>(At<float>(rec, 0x14)) - static_cast<double>(BitsFloat(time));
        At<float>(rec, 0x14) = static_cast<float>(left);
        if (!(left < 0.0 || left == 0.0)) {
            rec = next;
            next += 0x1C;
            continue;
        }
        bool done = false;
        std::uint32_t first, second;
        if (At<std::uint8_t>(rec, 0xC)) {
            second = 0;
            first = 1;
        } else {
            second = At<std::uint8_t>(rec, 0xD);
            first = second + 2;
        }
        PointerVector points;
        bool keep = false;
        if (Virtual<std::uint8_t>(At<void*>(self, field::kCells), 0x68, &points, At<void*>(rec, 4), first, second,
                                  0xC49u)) {
            const std::uint32_t count = At<std::uint32_t>(rec, 8);
            for (std::uint32_t i = 0; i < count && points.begin != points.end; ++i) {
                void* const pick = points.begin[Below(self, static_cast<std::uint32_t>(points.end - points.begin))];
                void* made;
                if (At<std::uint8_t>(rec, 0xC))
                    made = Virtual<void*>(self, v3::kCreateFor, pick, static_cast<std::uint32_t>(At<std::uint8_t>(rec, 0xD)),
                                          At<std::uint32_t>(rec, 0), At<std::uint32_t>(rec, 0x10), 0u, 0u);
                else {
                    std::uint32_t from[2] = {0, 0};
                    from[1] = Virtual<std::uint32_t>(pick, 0xC0);
                    std::uint32_t to[2] = {0, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(At<void*>(rec, 4)))};
                    float position[3] = {};
                    made = Virtual<void*>(self, v3::kDispatchNew, At<std::uint32_t>(rec, 0), from, to,
                                          At<std::uint32_t>(rec, 0x10), 3u, position, 0u);
                }
                if (!made)
                    continue;
                --At<std::uint32_t>(rec, 8);
                if (!pick)
                    continue;
                void** p = points.begin;
                while (p != points.end && *p != pick)
                    ++p;
                if (p == points.end)
                    continue;
                Release(pick);
                if (p + 1 != points.end)
                    std::memmove(p, p + 1, static_cast<std::size_t>(reinterpret_cast<char*>(points.end) -
                                                                    reinterpret_cast<char*>(p + 1)));
                --points.end;
            }
            if (At<std::uint32_t>(rec, 8) == 0)
                done = true;
            for (void** p = points.begin; p != points.end; ++p)
                Release(*p);
            points.end = points.begin;
        }
        (void)cellsObject;
        if (!done && ++At<std::uint32_t>(rec, 0x18) <= 5)
            keep = true;
        if (keep) {
            At<std::uint32_t>(rec, 0x14) = 0x447A0000u; // 1000.0f
            rec = next;
            next += 0x1C;
        } else {
            Release(At<void*>(rec, 4));
            char* const end = At<char*>(self, field::kRecordsEnd);
            if (next != end) {
                std::int32_t n = static_cast<std::int32_t>(end - next) / 0x1C;
                for (char* to = rec; n > 0; --n, to += 0x1C)
                    for (int k = 0; k < 7; ++k)
                        reinterpret_cast<std::uint32_t*>(to)[k] = reinterpret_cast<const std::uint32_t*>(to + 0x1C)[k];
            }
            At<char*>(self, field::kRecordsEnd) -= 0x1C;
        }
        if (points.begin)
            Free(points.begin);
    } while (rec != At<char*>(self, field::kRecordsEnd));
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

const Detour kCreate[] = {
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_GetRandomCreationPoint, GetRandomCreationPoint),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_DispatchAutomaton, DispatchAutomaton),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_DispatchNew, DispatchNew),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_AddRecord, AddRecord),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_CreateFor, CreateFor),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_OccupantPosition, OccupantPosition),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_KindMatrix, KindMatrix),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_SetAutomatonKind, SetAutomatonKind),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_SpawnAt, SpawnAt),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_SpawnNear, SpawnNear),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_SpawnFor, SpawnFor),
};

const Detour kFrame[] = {
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_OccupantRemoved, OccupantRemoved),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_OccupantAdded, VehicleOccupantAdded),
    OPENSC4_DETOUR(addr::cSC4PedestrianManager_OccupantAdded, PedestrianOccupantAdded),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_RemoveRecordsOf, RemoveRecordsOf),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_CopyRecords, CopyRecords),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_RefreshOccupants, RefreshOccupants),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_Update, Update),
    OPENSC4_DETOUR(addr::cSC4VehicleManager_Update, VehicleUpdate),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_UpdateRecordList, UpdateRecordList),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_ForgetAutomaton, ForgetAutomaton),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_KindSweep, KindSweep),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_CellBalance, CellBalance),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_MapInsert, MapInsert),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_MapAt, MapAt),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_CellSpawn, CellSpawn),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_CellCull, CellCull),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_LotSweep, LotSweep),
    OPENSC4_DETOUR(addr::cSC4VehPedManagerBase_UpdateRecords, UpdateRecords),
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

bool InstallCreate(registry::Mode) { return InstallList(kCreate, sizeof kCreate / sizeof kCreate[0], "create"); }

bool InstallFrame(registry::Mode) { return InstallList(kFrame, sizeof kFrame / sizeof kFrame[0], "frame"); }

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
    registry::Register({"simulators.automata.vehmgr.create", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehPedManagerBase_DispatchNew,
                        "Our vehicle and pedestrian managers' creation and dispatch: GetRandomCreationPoint, both "
                        "Dispatch, the record add (slot 70), the occupant's automaton (71) and its position, the kind "
                        "matrix (72), the automaton's kind set and the three spawns (79, 80, 89) (11 detours; part "
                        "v3, src/core/automata_vehmgr.cpp)",
                        false, InstallCreate}),
    registry::Register({"simulators.automata.vehmgr.frame", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4VehPedManagerBase_Update,
                        "Our vehicle and pedestrian managers' frames: the frame update (slot 63, base and vehicle) "
                        "with the record list's update, the occupant's removal (52) and arrival (53, both), the "
                        "records' removal (55) and copy, the occupants' refresh (56), the automaton's forgetting "
                        "(69), the kind sweep (76), the cell balance (77) with the tuning map's insert and lookup, "
                        "the cell spawn (78), the cell cull (81), the lot sweep (83) and the records' update (84) "
                        "(18 detours; part v3, src/core/automata_vehmgr.cpp)",
                        false, InstallFrame}),
};

} // namespace
} // namespace opensc4::automata_vehmgr
