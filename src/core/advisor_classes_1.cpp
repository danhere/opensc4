// The advisor classes derived from the advisor base, first part (0x00411800
// to 0x00416025): city planning (class 0x2A5F877D, 0xDC bytes), city
// situations (0x4BE372CD, 0xC0), finance (0xAA5292D7, 0xC4), health and
// education (0x6A5F8755, 0xC4) and safety (0xEA19E825, 0xC4). Factories,
// constructors, destructors, the Init and Shutdown overrides, the message
// handlers and the city planning advisor's Read and Write. Replacement, off
// by default:
//
//   scripts.advisors.classes1: the 35 functions of this part.
//
// The object: main vtable at +0, the advisor interface at +8, the
// serialisable part at +0xC, the message target at +0x10; the advisor base
// is 0xC0 bytes. The script-variable publishers, the base class and the
// containers' out of line helpers are called through their addresses.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::advisor_classes {
namespace {

namespace field { // from the object's start
constexpr std::size_t kActive = 0x78;      // map of advice to record index
constexpr std::size_t kRecords = 0x84;     // vector of 16-byte records
constexpr std::size_t kDirty = 0xB8;
constexpr std::size_t kUpdateFlags = 0xBC; // bit 0: an update is requested
constexpr std::size_t kListening = 0xC0;   // the derived classes' byte
// City planning only.
constexpr std::size_t kBuildings = 0xC4;
constexpr std::size_t kParks = 0xC8;
constexpr std::size_t kRecreation = 0xCC;
constexpr std::size_t kCounts = 0xD0;      // map of int to int: header, count
} // namespace field

constexpr std::size_t kInterface = 0x8;
constexpr std::size_t kSerializable = 0xC;
constexpr std::size_t kTarget = 0x10;

constexpr uint32_t kOccupantInserted = 0x99EF1142;
constexpr uint32_t kOccupantRemoved = 0x99EF1143;
constexpr uint32_t kUpdateRequest = 0xCA8F3DB6;
constexpr uint32_t kBuildingOccupant = 0x278128A0;

// The four vtables of a class, in the object's order.
struct Vtables {
    std::uintptr_t main;
    std::uintptr_t advisor;
    std::uintptr_t serializable;
    std::uintptr_t target;
};

constexpr Vtables kCityPlanning = {
    addr::cSC4AdvisorCityPlanning_Vtable,
    addr::cSC4AdvisorCityPlanning_InterfaceVtable,
    addr::cSC4AdvisorCityPlanning_SerializableVtable,
    addr::cSC4AdvisorCityPlanning_MessageTargetVtable,
};
constexpr Vtables kCitySituations = {
    addr::cSC4AdvisorCitySituations_Vtable,
    addr::cSC4AdvisorCitySituations_InterfaceVtable,
    addr::cSC4AdvisorCitySituations_SerializableVtable,
    addr::cSC4AdvisorCitySituations_MessageTargetVtable,
};
constexpr Vtables kFinance = {
    addr::cSC4AdvisorFinance_Vtable,
    addr::cSC4AdvisorFinance_InterfaceVtable,
    addr::cSC4AdvisorFinance_SerializableVtable,
    addr::cSC4AdvisorFinance_MessageTargetVtable,
};
constexpr Vtables kHealthEducation = {
    addr::cSC4AdvisorHQ_Vtable,
    addr::cSC4AdvisorHQ_InterfaceVtable,
    addr::cSC4AdvisorHQ_SerializableVtable,
    addr::cSC4AdvisorHQ_MessageTargetVtable,
};
constexpr Vtables kSafety = {
    addr::cSC4AdvisorSafety_Vtable,
    addr::cSC4AdvisorSafety_InterfaceVtable,
    addr::cSC4AdvisorSafety_SerializableVtable,
    addr::cSC4AdvisorSafety_MessageTargetVtable,
};

// A map's insert result and its value.
struct InsertResult {
    char* node;
    uint8_t inserted;
};

struct IntPair {
    int32_t key;
    int32_t value;
};

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void* OperatorNew(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(size);
}

void OperatorDelete(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(memory);
}

// The advisor base's constructor and destructor (the game's).
char* BaseConstruct(char* obj)
{
    return reinterpret_cast<char*(__thiscall*)(char*)>(addr::cSC4Advisor_F40FFF0)(obj);
}

void BaseDestroy(char* obj)
{
    reinterpret_cast<void(__thiscall*)(char*)>(addr::cSC4Advisor_F40FCC0)(obj);
}

// The advisor base's Init and Shutdown on the interface at +8.
uint8_t BaseInit(char* advisor, uint32_t argument)
{
    return reinterpret_cast<uint8_t(__thiscall*)(char*, uint32_t)>(addr::cSC4Advisor_F410E30)(advisor, argument);
}

uint32_t BaseShutdown(char* advisor)
{
    return reinterpret_cast<uint32_t(__thiscall*)(char*)>(addr::cSC4Advisor_F40EF30)(advisor);
}

uint32_t BaseDoMessage(char* target, void* message)
{
    return reinterpret_cast<uint32_t(__thiscall*)(char*, void*)>(addr::cSC4Advisor_DoMessage)(target, message);
}

void RequestUpdate(char* obj, uint32_t a, uint32_t b)
{
    reinterpret_cast<void(__thiscall*)(char*, uint32_t, uint32_t)>(addr::cSC4Advisor_RequestUpdate)(obj, a, b);
}

// The message target of the advisor whose interface is `advisor`, as the
// game's cast works it out.
char* TargetOf(char* advisor)
{
    return (advisor - kInterface) ? advisor + 0x8 : nullptr;
}

void AddNotification(char* advisor, uint32_t type)
{
    char* const target = TargetOf(advisor);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x14, target, type);
}

void RemoveNotification(char* advisor, uint32_t type)
{
    char* const target = TargetOf(advisor);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x18, target, type);
}

// Empties the tree at `map` (header, count) as the game's inlined clear.
void ClearTree(char* map)
{
    using EraseFn = void(__thiscall*)(char*, char*);
    reinterpret_cast<EraseFn>(addr::SC4Uint32Map_EraseSubtree2)(map, At<char*>(At<char*>(map, 0x0), 0x4));
    At<char*>(At<char*>(map, 0x0), 0x8) = At<char*>(map, 0x0);
    At<char*>(At<char*>(map, 0x0), 0x4) = nullptr;
    At<char*>(At<char*>(map, 0x0), 0xC) = At<char*>(map, 0x0);
    At<uint32_t>(map, 0x4) = 0;
}

void InsertPair(char* map, InsertResult* result, const IntPair* value)
{
    using InsertFn = InsertResult*(__thiscall*)(char*, InsertResult*, const IntPair*);
    reinterpret_cast<InsertFn>(addr::cSC4Advisors_F412040)(map, result, value);
}

// The scalar deleting destructor of the three classes without a destructor
// body of their own.
char* DeleteWith(char* self, const Vtables& vtables, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = vtables.main;
    At<std::uintptr_t>(self, kInterface) = vtables.advisor;
    At<std::uintptr_t>(self, kSerializable) = vtables.serializable;
    At<std::uintptr_t>(self, kTarget) = vtables.target;
    BaseDestroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// The occupant messages' common end: asks for an update once, then the
// base handler.
void RequestOnce(char* target)
{
    if (At<uint32_t>(target, field::kUpdateFlags - kTarget) == 0)
        RequestUpdate(target - kTarget, 0, 0);
    At<uint32_t>(target, field::kUpdateFlags - kTarget) |= 1;
}

// ---------------------------------------------------------------- city planning

// The game's 0x00411800.
uint32_t __cdecl CityPlanningClassId()
{
    return 0x2A5F877D;
}

// The game's 0x00411810: QueryInterface on the object.
uint8_t __fastcall CityPlanningQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 0x2A5F877D) {
        using Fn = uint8_t(__thiscall*)(char*, uint32_t, void**);
        return reinterpret_cast<Fn>(addr::SC4AgentBase_QueryInterface)(self, iid, out);
    }
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x00411EE0, 0x00411EF0 and 0x00411F00: the same on the
// interface, the serialisable part and the message target.
uint8_t __fastcall CityPlanningQueryInterface8(char* self, void*, uint32_t iid, void** out)
{
    return CityPlanningQueryInterface(self - kInterface, nullptr, iid, out);
}

uint8_t __fastcall CityPlanningQueryInterfaceC(char* self, void*, uint32_t iid, void** out)
{
    return CityPlanningQueryInterface(self - kSerializable, nullptr, iid, out);
}

uint8_t __fastcall CityPlanningQueryInterface10(char* self, void*, uint32_t iid, void** out)
{
    return CityPlanningQueryInterface(self - kTarget, nullptr, iid, out);
}

constexpr uint32_t kCityPlanningMessages[] = {
    0xEA394B74, 0xCB3BE815, 0xEB3BE415, 0x2B3BE44A, 0x6B3BE6D8, 0x0B3BE81A, 0x8A394DC4, 0x4A490D33,
};

// The game's 0x004118C0: Init, on the interface at +8.
uint8_t __fastcall CityPlanningInit(char* self, void*, uint32_t argument)
{
    if (!BaseInit(self, argument))
        return 0;
    const uint8_t listening = At<uint8_t>(self, field::kListening - kInterface);
    At<uint8_t>(self, field::kDirty - kInterface) = 1;
    if (listening)
        return 1;
    At<uint8_t>(self, field::kListening - kInterface) = 1;
    for (const uint32_t type : kCityPlanningMessages)
        AddNotification(self, type);
    return 1;
}

// The game's 0x004119F0: Shutdown, on the interface at +8.
uint32_t __fastcall CityPlanningShutdown(char* self, void*)
{
    if (At<uint8_t>(self, field::kListening - kInterface)) {
        At<uint8_t>(self, field::kListening - kInterface) = 0;
        for (const uint32_t type : kCityPlanningMessages)
            RemoveNotification(self, type);
    }
    return BaseShutdown(self);
}

// The game's 0x00411C80: Write, on the serialisable part at +0xC.
uint32_t __fastcall CityPlanningWrite(char* self, void*, void* stream)
{
    char* const obj = self - kSerializable;
    Virtual<uint8_t>(stream, 0x1C, uint32_t{3});
    if (reinterpret_cast<uint8_t(__thiscall*)(char*, void*)>(addr::SC4AgentBase_Write)(self, stream)) {
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(obj, field::kBuildings));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(obj, field::kParks));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(obj, field::kRecreation));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(obj, field::kCounts + 0x4));
        char* node = At<char*>(At<char*>(obj, field::kCounts), 0x8);
        if (node != At<char*>(obj, field::kCounts)) {
            do {
                Virtual<uint8_t>(stream, 0x20, At<uint32_t>(node, 0x10));
                Virtual<uint8_t>(stream, 0x20, At<uint32_t>(node, 0x14));
                // The next node in order.
                char* next = node;
                char* other = At<char*>(node, 0xC);
                if (other) {
                    next = other;
                    while (At<char*>(next, 0x8))
                        next = At<char*>(next, 0x8);
                } else {
                    other = At<char*>(node, 0x4);
                    if (node == At<char*>(other, 0xC)) {
                        do {
                            next = other;
                            other = At<char*>(other, 0x4);
                        } while (next == At<char*>(other, 0xC));
                    }
                    if (At<char*>(next, 0xC) != other)
                        next = other;
                }
                node = next;
            } while (node != At<char*>(obj, field::kCounts));
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The game's 0x00411E70: the destructor.
void __fastcall CityPlanningDestroy(char* self, void*)
{
    char* const map = self + field::kCounts;
    At<std::uintptr_t>(self, 0x0) = kCityPlanning.main;
    At<std::uintptr_t>(self, kInterface) = kCityPlanning.advisor;
    At<std::uintptr_t>(self, kSerializable) = kCityPlanning.serializable;
    At<std::uintptr_t>(self, kTarget) = kCityPlanning.target;
    if (At<uint32_t>(map, 0x4))
        ClearTree(map);
    if (char* const header = At<char*>(map, 0x0))
        Free(header);
    BaseDestroy(self);
}

// The game's 0x00412100: the constructor.
char* __fastcall CityPlanningConstruct(char* self, void*)
{
    BaseConstruct(self);
    At<std::uintptr_t>(self, 0x0) = kCityPlanning.main;
    At<std::uintptr_t>(self, kInterface) = kCityPlanning.advisor;
    At<std::uintptr_t>(self, kSerializable) = kCityPlanning.serializable;
    At<std::uintptr_t>(self, kTarget) = kCityPlanning.target;
    At<uint8_t>(self, field::kListening) = 0;
    At<uint32_t>(self, field::kBuildings) = 0;
    At<uint32_t>(self, field::kParks) = 0;
    At<uint32_t>(self, field::kRecreation) = 0;
    At<char*>(self, field::kCounts) = nullptr;
    char* const header =
        static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0x18));
    At<char*>(self, field::kCounts) = header;
    At<uint32_t>(self, field::kCounts + 0x4) = 0;
    At<uint8_t>(header, 0x0) = 0;
    At<char*>(At<char*>(self, field::kCounts), 0x4) = nullptr;
    At<char*>(At<char*>(self, field::kCounts), 0x8) = At<char*>(self, field::kCounts);
    At<char*>(At<char*>(self, field::kCounts), 0xC) = At<char*>(self, field::kCounts);
    return self;
}

// The game's 0x00412180: the scalar deleting destructor.
char* __fastcall CityPlanningDelete(char* self, void*, uint32_t flags)
{
    CityPlanningDestroy(self, nullptr);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// The game's 0x004121A0: the class factory; returns the interface at +8.
char* __cdecl CityPlanningCreate()
{
    char* const memory = static_cast<char*>(OperatorNew(0xDC));
    if (memory) {
        if (char* const obj = CityPlanningConstruct(memory, nullptr))
            return obj + kInterface;
    }
    return nullptr;
}

// The game's 0x004121D0: DoMessage, on the message target at +0x10. Counts
// buildings by kind as they come and go.
uint32_t __fastcall CityPlanningDoMessage(char* self, void*, void* message)
{
    char* const obj = self - kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kUpdateRequest) {
        if (At<uint8_t>(obj, field::kUpdateFlags) & 1)
            reinterpret_cast<void(__thiscall*)(char*)>(addr::ScriptGlobals_PublishParkGlobals)(obj);
        At<uint32_t>(obj, field::kUpdateFlags) = 0;
        BaseDoMessage(self, message);
        return 1;
    }
    if (type != kOccupantInserted && type != kOccupantRemoved)
        return BaseDoMessage(self, message);

    void* const subject = Virtual<void*>(message, 0x28);
    if (Virtual<uint32_t>(subject, 0x1C) != kBuildingOccupant)
        return 1;
    const int32_t step = type == kOccupantInserted ? 1 : -1;
    At<int32_t>(obj, field::kBuildings) += step;
    if (Virtual<uint8_t>(subject, 0x58, uint32_t{0x1006}))
        At<int32_t>(obj, field::kParks) += step;
    if (Virtual<uint8_t>(subject, 0x58, uint32_t{0x1006}))
        At<int32_t>(obj, field::kRecreation) += step;
    if (Virtual<uint8_t>(subject, 0x58, uint32_t{0x150A})) {
        void* reference = nullptr;
        uint32_t where[3] = {0, 0, 0};
        if (Virtual<uint8_t>(subject, 0x0, uint32_t{0x87DDA3A9}, &reference)) {
            if (void* const service = Global<void*>(addr::SC4_Service43D68)) {
                void* const got = Virtual<void*>(reference, 0x10, &where[0]);
                if (Virtual<uint8_t>(service, 0x38, got)) {
                    char* const map = obj + field::kCounts;
                    InsertResult result;
                    IntPair value;
                    value.key = static_cast<int32_t>(where[2]);
                    using FindFn = void(__thiscall*)(char*, char**, const int32_t*);
                    reinterpret_cast<FindFn>(addr::SC4Sint32Map_Find)(map, &result.node, &value.key);
                    char* const header = At<char*>(map, 0x0);
                    if (type == kOccupantRemoved) {
                        if (result.node != header)
                            --At<int32_t>(result.node, 0x14);
                    } else {
                        if (result.node == header) {
                            value.key = static_cast<int32_t>(where[2]);
                            value.value = 0;
                            InsertPair(map, &result, &value);
                        }
                        ++At<int32_t>(result.node, 0x14);
                    }
                }
            }
        }
        if (reference)
            Release(reference);
    }
    RequestOnce(self);
    BaseDoMessage(self, message);
    return 1;
}

// The game's 0x004123E0: Read, on the serialisable part at +0xC.
uint32_t __fastcall CityPlanningRead(char* self, void*, void* stream)
{
    char* const obj = self - kSerializable;
    // Neither local is set before the stream fills it, as in the original.
    uint16_t version;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (reinterpret_cast<uint8_t(__thiscall*)(char*, void*)>(addr::SC4AgentBase_Read)(self, stream)) {
        Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kBuildings));
        Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kParks));
        Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kRecreation));
        if (version > 2) {
            char* const map = obj + field::kCounts;
            if (At<uint32_t>(map, 0x4) != 0)
                ClearTree(map);
            uint32_t left;
            Virtual<uint8_t>(stream, 0x20, &left);
            while (left != 0) {
                --left;
                int32_t key = 0;
                int32_t count = 0;
                Virtual<uint8_t>(stream, 0x20, &key);
                Virtual<uint8_t>(stream, 0x20, &count);
                IntPair value;
                value.key = key;
                value.value = count;
                InsertResult result;
                InsertPair(map, &result, &value);
            }
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// -------------------------------------------------------------- city situations

// The game's 0x00413680.
uint32_t __cdecl CitySituationsClassId()
{
    return 0x4BE372CD;
}

// The game's 0x004136B0: the class factory.
char* __cdecl CitySituationsCreate()
{
    char* const obj = static_cast<char*>(OperatorNew(0xC0));
    if (!obj)
        return nullptr;
    BaseConstruct(obj);
    At<std::uintptr_t>(obj, 0x0) = kCitySituations.main;
    At<std::uintptr_t>(obj, kSerializable) = kCitySituations.serializable;
    At<std::uintptr_t>(obj, kTarget) = kCitySituations.target;
    At<std::uintptr_t>(obj, kInterface) = kCitySituations.advisor;
    return obj + kInterface;
}

// The game's 0x004136F0: the scalar deleting destructor.
char* __fastcall CitySituationsDelete(char* self, void*, uint32_t flags)
{
    return DeleteWith(self, kCitySituations, flags);
}

// The game's 0x00413730: slot 20 of the interface at +8, (advice), ret 4.
// Takes an advice with flag 2 out of the active map and hands its record
// to 0x0040DF90.
uint8_t __fastcall CitySituationsRetire(char* self, void*, void* advice)
{
    char* const obj = self - kInterface;
    if (!advice || !Virtual<uint8_t>(advice, 0x3C, uint32_t{2}))
        return 0;
    void* held = advice;
    Virtual<uint32_t>(advice, 0x4);
    char* const map = obj + field::kActive;
    char* const node =
        reinterpret_cast<char*(__thiscall*)(char*, void**)>(addr::cSC4Advisor_F40DE90)(map, &held);
    Release(advice);
    if (node != At<char*>(map, 0x0)) {
        char* const begin = At<char*>(obj, field::kRecords);
        const uint32_t index = At<uint32_t>(node, 0x14);
        if (index < static_cast<uint32_t>(static_cast<int32_t>(At<char*>(obj, field::kRecords + 0x4) - begin) >> 4)) {
            reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40E440)(map, node);
            reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40DF90)(obj, begin + (index << 4));
        }
    }
    return 1;
}

// The game's 0x004137C0: slot 10 of the main vtable, no arguments. Sorts
// the records whose advice has flag 0x20 into active and not, by the
// advice's own test (its slot 8).
void __fastcall CitySituationsUpdate(char* self, void*)
{
    void* const first = Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C);
    if (!first)
        return;
    // The original passes the address of a local it never set.
    uint32_t scratch;
    if (void* const second = Virtual<void*>(first, 0x30, &scratch))
        Virtual<uint32_t>(second, 0x110);
    const int32_t count =
        static_cast<int32_t>(At<char*>(self, field::kRecords + 0x4) - At<char*>(self, field::kRecords)) >> 4;
    if (count <= 0)
        return;
    std::size_t offset = 0;
    int32_t i = 0;
    do {
        char* const record = At<char*>(self, field::kRecords) + offset;
        void* kind = nullptr;
        void* const advice = At<void*>(record, 0x0);
        if (advice && Virtual<uint8_t>(advice, 0x0, uint32_t{0x6BEB1E8A}, &kind) &&
            (Virtual<uint32_t>(kind, 0x38) & 0x20)) {
            char* const map = self + field::kActive;
            if (Virtual<uint8_t>(At<void*>(record, 0x0), 0x20)) {
                *reinterpret_cast<int32_t*(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40EFF0)(map, record) = i;
                reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40DF20)(self, record);
            } else {
                char* const node =
                    reinterpret_cast<char*(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40DE90)(map, record);
                if (node != At<char*>(map, 0x0))
                    reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40E440)(map, node);
                reinterpret_cast<void(__thiscall*)(char*, char*)>(addr::cSC4Advisor_F40DF90)(self, record);
            }
        }
        offset += 0x10;
        ++i;
        if (kind)
            Release(kind);
    } while (i < count);
}

// ---------------------------------------------------------------------- finance

// The game's 0x004138D0.
uint32_t __cdecl FinanceClassId()
{
    return 0xAA5292D7;
}

// The game's 0x004138E0: DoMessage, on the message target at +0x10.
uint32_t __fastcall FinanceDoMessage(char* self, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kUpdateRequest) {
        At<uint32_t>(self, field::kUpdateFlags - kTarget) = 0;
        BaseDoMessage(self, message);
        return 1;
    }
    if (type != kOccupantInserted && type != kOccupantRemoved)
        return BaseDoMessage(self, message);
    void* const subject = Virtual<void*>(message, 0x28);
    if (Virtual<uint32_t>(subject, 0x1C) == kBuildingOccupant)
        RequestOnce(self);
    BaseDoMessage(self, message);
    return 1;
}

// The game's 0x00413970: Init of the city situations and finance advisors,
// on the interface at +8.
uint32_t __fastcall DirtyInit(char* self, void*, uint32_t argument)
{
    At<uint8_t>(self, field::kDirty - kInterface) = 1;
    return reinterpret_cast<uint32_t(__thiscall*)(char*, uint32_t)>(addr::cSC4Advisor_F410E30)(self, argument);
}

// The game's 0x00413980: the class factory.
char* __cdecl FinanceCreate()
{
    char* const obj = static_cast<char*>(OperatorNew(0xC4));
    if (!obj)
        return nullptr;
    BaseConstruct(obj);
    At<std::uintptr_t>(obj, 0x0) = kFinance.main;
    At<std::uintptr_t>(obj, kSerializable) = kFinance.serializable;
    At<std::uintptr_t>(obj, kTarget) = kFinance.target;
    At<uint8_t>(obj, field::kListening) = 0;
    At<std::uintptr_t>(obj, kInterface) = kFinance.advisor;
    return obj + kInterface;
}

// The game's 0x004139D0: the scalar deleting destructor.
char* __fastcall FinanceDelete(char* self, void*, uint32_t flags)
{
    return DeleteWith(self, kFinance, flags);
}

// --------------------------------------------------------- health and education

// The building group keys the publishers use (vectors of group numbers),
// in the order the original fills and empties them, with each one's part
// of the table at 0x00A81F10 (offsets in bytes).
struct GroupKey {
    std::uintptr_t vector;
    uint32_t first;
    uint32_t last;
};

constexpr GroupKey kGroupKeys[] = {
    {addr::ScriptGlobals_ElementaryKey, 0x00, 0x04}, {addr::ScriptGlobals_HighSchoolKey, 0x04, 0x08},
    {addr::ScriptGlobals_CollegeKey, 0x08, 0x0C},    {addr::ScriptGlobals_EducationKey, 0x1C, 0x34},
    {addr::ScriptGlobals_SchoolKey, 0x0C, 0x1C},     {addr::ScriptGlobals_MuseumKey, 0x34, 0x38},
    {addr::ScriptGlobals_LibraryKey, 0x38, 0x3C},    {addr::ScriptGlobals_HospitalKey, 0x3C, 0x40},
    {addr::ScriptGlobals_ClinicKey, 0x40, 0x44},     {addr::ScriptGlobals_HealthKey, 0x44, 0x4C},
};

// The game's 0x00413D30.
uint32_t __cdecl HealthEducationClassId()
{
    return 0x6A5F8755;
}

// The game's 0x00413D40: the class factory.
char* __cdecl HealthEducationCreate()
{
    char* const obj = static_cast<char*>(OperatorNew(0xC4));
    if (!obj)
        return nullptr;
    BaseConstruct(obj);
    At<std::uintptr_t>(obj, 0x0) = kHealthEducation.main;
    At<std::uintptr_t>(obj, kSerializable) = kHealthEducation.serializable;
    At<std::uintptr_t>(obj, kTarget) = kHealthEducation.target;
    At<uint8_t>(obj, field::kListening) = 0;
    At<std::uintptr_t>(obj, kInterface) = kHealthEducation.advisor;
    return obj + kInterface;
}

// The game's 0x00413D90: the scalar deleting destructor.
char* __fastcall HealthEducationDelete(char* self, void*, uint32_t flags)
{
    return DeleteWith(self, kHealthEducation, flags);
}

// The game's 0x00413EE0: Shutdown, on the interface at +8. Empties the
// group keys.
uint32_t __fastcall HealthEducationShutdown(char* self, void*)
{
    if (At<uint8_t>(self, field::kListening - kInterface)) {
        At<uint8_t>(self, field::kListening - kInterface) = 0;
        for (const GroupKey& key : kGroupKeys)
            Global<uint32_t*>(key.vector + 0x4) = Global<uint32_t*>(key.vector);
    }
    return BaseShutdown(self);
}

// The game's 0x00415960: DoMessage, on the message target at +0x10.
uint32_t __fastcall HealthEducationDoMessage(char* self, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kUpdateRequest) {
        if (At<uint8_t>(self, field::kUpdateFlags - kTarget) & 1) {
            using PublishFn = void(__thiscall*)(char*);
            reinterpret_cast<PublishFn>(addr::ScriptGlobals_PublishEducationBuildings)(self - kTarget);
        }
        At<uint32_t>(self, field::kUpdateFlags - kTarget) = 0;
        BaseDoMessage(self, message);
        return 1;
    }
    if (type != kOccupantInserted && type != kOccupantRemoved)
        return BaseDoMessage(self, message);
    void* const subject = Virtual<void*>(message, 0x28);
    if (Virtual<uint32_t>(subject, 0x1C) == kBuildingOccupant)
        RequestOnce(self);
    BaseDoMessage(self, message);
    return 1;
}

// The game's 0x00415A00: Init, on the interface at +8. Fills the group
// keys from the table.
uint8_t __fastcall HealthEducationInit(char* self, void*, uint32_t argument)
{
    if (!BaseInit(self, argument))
        return 0;
    const uint8_t listening = At<uint8_t>(self, field::kListening - kInterface);
    At<uint8_t>(self, field::kDirty - kInterface) = 1;
    if (listening)
        return 1;
    At<uint8_t>(self, field::kListening - kInterface) = 1;
    for (const GroupKey& key : kGroupKeys) {
        // The last argument is the address of Init's own argument slot.
        using InsertFn = void(__thiscall*)(void*, uint32_t*, const uint32_t*, const uint32_t*, uint32_t*);
        reinterpret_cast<InsertFn>(addr::SC4Uint32Vector_InsertRange)(
            reinterpret_cast<void*>(key.vector), Global<uint32_t*>(key.vector + 0x4),
            reinterpret_cast<const uint32_t*>(addr::ScriptGlobals_BuildingGroupTable + key.first),
            reinterpret_cast<const uint32_t*>(addr::ScriptGlobals_BuildingGroupTable + key.last), &argument);
    }
    return 1;
}

// ----------------------------------------------------------------------- safety

// The game's 0x00415B80.
uint32_t __cdecl SafetyClassId()
{
    return 0xEA19E825;
}

// The game's 0x00415B90: the class factory.
char* __cdecl SafetyCreate()
{
    char* const obj = static_cast<char*>(OperatorNew(0xC4));
    if (!obj)
        return nullptr;
    BaseConstruct(obj);
    At<uint8_t>(obj, 0xC0) = 0;
    At<uint8_t>(obj, 0xC1) = 0;
    At<uint8_t>(obj, 0xC2) = 0;
    At<uint8_t>(obj, 0xC3) = 0;
    At<std::uintptr_t>(obj, 0x0) = kSafety.main;
    At<std::uintptr_t>(obj, kSerializable) = kSafety.serializable;
    At<std::uintptr_t>(obj, kTarget) = kSafety.target;
    At<std::uintptr_t>(obj, kInterface) = kSafety.advisor;
    return obj + kInterface;
}

// The game's 0x00415BF0: the scalar deleting destructor.
char* __fastcall SafetyDelete(char* self, void*, uint32_t flags)
{
    return DeleteWith(self, kSafety, flags);
}

constexpr uint32_t kSafetyMessages[] = {
    0xEA1F0DCD, 0xA8DAF511, 0xA8DAF512, 0x2A614891, 0x2A614892, 0xAA667CE2, 0x68F54CE1, 0x68F54CE2,
    0xCA1B1E64, 0xAA1B2002, 0x69247DC4, 0x69247DC5, 0x077ACCD0, 0x077ACCD2, 0xCA8DCDB4, 0xAA552F77,
};

// The game's 0x00415C30: Init, on the interface at +8.
uint8_t __fastcall SafetyInit(char* self, void*, uint32_t argument)
{
    if (!BaseInit(self, argument))
        return 0;
    const uint8_t listening = At<uint8_t>(self, field::kListening - kInterface);
    At<uint8_t>(self, field::kDirty - kInterface) = 1;
    if (listening)
        return 1;
    for (const uint32_t type : kSafetyMessages)
        AddNotification(self, type);
    At<uint8_t>(self, field::kListening - kInterface) = 1;
    return 1;
}

// The game's 0x00415E40: Shutdown, on the interface at +8.
uint32_t __fastcall SafetyShutdown(char* self, void*)
{
    if (At<uint8_t>(self, field::kListening - kInterface)) {
        for (const uint32_t type : kSafetyMessages)
            RemoveNotification(self, type);
        At<uint8_t>(self, field::kListening - kInterface) = 0;
    }
    return BaseShutdown(self);
}

bool InstallClasses1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[35] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advisors_F411800, reinterpret_cast<void*>(&CityPlanningClassId)},
        {addr::cSC4Advisors_F411810, reinterpret_cast<void*>(&CityPlanningQueryInterface)},
        {addr::cSC4Advisors_F4118C0, reinterpret_cast<void*>(&CityPlanningInit)},
        {addr::cSC4Advisors_F4119F0, reinterpret_cast<void*>(&CityPlanningShutdown)},
        {addr::cSC4Advisors_F411C80, reinterpret_cast<void*>(&CityPlanningWrite)},
        {addr::cSC4Advisors_F411E70, reinterpret_cast<void*>(&CityPlanningDestroy)},
        {addr::cSC4Advisors_F411EE0, reinterpret_cast<void*>(&CityPlanningQueryInterface8)},
        {addr::cSC4Advisors_F411EF0, reinterpret_cast<void*>(&CityPlanningQueryInterfaceC)},
        {addr::cSC4Advisors_F411F00, reinterpret_cast<void*>(&CityPlanningQueryInterface10)},
        {addr::cSC4Advisors_F412100, reinterpret_cast<void*>(&CityPlanningConstruct)},
        {addr::cSC4Advisors_F412180, reinterpret_cast<void*>(&CityPlanningDelete)},
        {addr::cSC4Advisors_F4121A0, reinterpret_cast<void*>(&CityPlanningCreate)},
        {addr::cSC4Advisors_F4121D0, reinterpret_cast<void*>(&CityPlanningDoMessage)},
        {addr::cSC4Advisors_F4123E0, reinterpret_cast<void*>(&CityPlanningRead)},
        {addr::cSC4Advisors_F413680, reinterpret_cast<void*>(&CitySituationsClassId)},
        {addr::cSC4Advisors_F4136B0, reinterpret_cast<void*>(&CitySituationsCreate)},
        {addr::cSC4Advisors_F4136F0, reinterpret_cast<void*>(&CitySituationsDelete)},
        {addr::cSC4Advisors_F413730, reinterpret_cast<void*>(&CitySituationsRetire)},
        {addr::cSC4Advisors_F4137C0, reinterpret_cast<void*>(&CitySituationsUpdate)},
        {addr::cSC4Advisors_F4138D0, reinterpret_cast<void*>(&FinanceClassId)},
        {addr::cSC4Advisors_F4138E0, reinterpret_cast<void*>(&FinanceDoMessage)},
        {addr::cSC4Advisors_F413970, reinterpret_cast<void*>(&DirtyInit)},
        {addr::cSC4Advisors_F413980, reinterpret_cast<void*>(&FinanceCreate)},
        {addr::cSC4Advisors_F4139D0, reinterpret_cast<void*>(&FinanceDelete)},
        {addr::cSC4Advisors_F413D30, reinterpret_cast<void*>(&HealthEducationClassId)},
        {addr::cSC4Advisors_F413D40, reinterpret_cast<void*>(&HealthEducationCreate)},
        {addr::cSC4Advisors_F413D90, reinterpret_cast<void*>(&HealthEducationDelete)},
        {addr::cSC4Advisors_F413EE0, reinterpret_cast<void*>(&HealthEducationShutdown)},
        {addr::cSC4Advisors_F415960, reinterpret_cast<void*>(&HealthEducationDoMessage)},
        {addr::cSC4Advisors_F415A00, reinterpret_cast<void*>(&HealthEducationInit)},
        {addr::cSC4Advisors_F415B80, reinterpret_cast<void*>(&SafetyClassId)},
        {addr::cSC4Advisors_F415B90, reinterpret_cast<void*>(&SafetyCreate)},
        {addr::cSC4Advisors_F415BF0, reinterpret_cast<void*>(&SafetyDelete)},
        {addr::cSC4Advisors_F415C30, reinterpret_cast<void*>(&SafetyInit)},
        {addr::cSC4Advisors_F415E40, reinterpret_cast<void*>(&SafetyShutdown)},
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

const bool g_classes1Registered = registry::Register({
    "scripts.advisors.classes1",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Advisors_F4118C0,
    "Our advisor classes, first part: city planning, city situations, finance, health and education, safety "
    "(docs/lua.md)",
    false,
    InstallClasses1,
});

} // namespace
} // namespace opensc4::advisor_classes
