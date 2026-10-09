// The fire and police simulators' dispatch sources (docs/fire-police.md):
// fire planes (at fire +0xD0, class 0x4C0AE114) and police helicopters (at
// police +0xC8, class 0x0C0B06E8). Both are 0x28-byte objects with the same
// layout and mostly the same code; they keep the buildings that hold
// aircraft and their units' dispatch slots, as the protection base does for
// stations, but with properties of their own.
//
// Layout: +0 dispatch interface, +4 message target, +8 cIGZSerializable,
// +0xC initialised, +0x10 references, +0x14 kind (5 fire, 6 police), +0x18
// the city (held), +0x1C buildings (list), +0x20 message server, +0x24
// dispatch slots in use.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::protection_sources {
namespace {

namespace field {
constexpr std::size_t kInitialised = 0xC;  // uint8
constexpr std::size_t kReferences = 0x10;
constexpr std::size_t kKind = 0x14;
constexpr std::size_t kCity = 0x18;
constexpr std::size_t kBuildings = 0x1C;   // list
constexpr std::size_t kMessageServer = 0x20;
constexpr std::size_t kSlots = 0x24;
} // namespace field

constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kRegistered[] = {0x99EF1142, 0x99EF1143, 0xE94671EB, 0x894671F6, 0xE94671EC, 0xE94671ED};
constexpr uint32_t kUnregistered[] = {0x99EF1142, 0x99EF1143, 0xE94671EB, 0xE94671EC, 0xE94671ED, 0x894671F6};

using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
using SetUint32Fn = uint32_t(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using SelfFn = uint32_t(__thiscall*)(void* self);
const auto GetPropertyUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
const auto GetPropertySint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
const auto SetPropertyUint32 = reinterpret_cast<SetUint32Fn>(addr::SC4_SetPropertyUint32);
const auto Free = reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free);

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void* Properties(void* occupant)
{
    return Virtual<void*>(occupant, 0x18);
}

// The properties of one kind of source.
struct Kind {
    uint32_t count;
    uint32_t mask;
    uint32_t first;
};
constexpr Kind kPlanes{0x0C0AFDD7, 0xCC0AFDDB, 0x0C0AFF3F};
constexpr Kind kHelicopters{0xCC0B0705, 0xEC0B0709, 0xAC0B070E};

// --- Identity and lifetime ---------------------------------------------------------

// 0x006AC550 (both kinds; the other interfaces reach it through thunks).
uint8_t __fastcall QueryInterface(char* source, void*, uint32_t iid, void** out)
{
    char* found;
    switch (iid) {
    case 0xC9C00435:
        found = source;
        break;
    case 0x452294AA:
        found = source ? source + 4 : nullptr;
        break;
    case 0xE4FDA3D4:
        found = source ? source + 8 : nullptr;
        break;
    default:
        return 0;
    }
    *out = found;
    Virtual<uint32_t>(source, 0x4);
    return 1;
}

// 0x006DFAF0: at the last reference, Shutdown and the deleting destructor.
uint32_t __fastcall ReleaseSelf(char* source, void*)
{
    int32_t& references = At<int32_t>(source, field::kReferences);
    if (references > 1)
        return static_cast<uint32_t>(--references);
    reinterpret_cast<SelfFn>(addr::SC4DispatchSource_Shutdown)(source);
    Virtual<void*>(source, 0x30, 1u);
    return 0;
}

uint32_t __fastcall PlanesClass(void*, void*)
{
    return 0x4C0AE114;
}

uint32_t __fastcall HelicoptersClass(void*, void*)
{
    return 0x0C0B06E8;
}

// --- Init (0x006DF8C0) and Shutdown (0x006AC860) --------------------------------

uint8_t __fastcall Init(char* source, void*)
{
    if (source[field::kInitialised])
        return 1;
    source[field::kInitialised] = 1;
    if (!At<void*>(source, field::kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(source, field::kCity) = city;
        Virtual<uint32_t>(city, 0x4);
    }
    if (!At<void*>(source, field::kMessageServer)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        At<void*>(source, field::kMessageServer) = server;
        Virtual<uint32_t>(server, 0x4);
        for (const uint32_t message : kRegistered)
            Virtual<uint8_t>(At<void*>(source, field::kMessageServer), 0x14, source + 4, message);
    }
    Virtual<uint32_t>(Global<void*>(addr::SC4_DispatchService), 0x34, source);
    return 1;
}

uint8_t __fastcall Shutdown(char* source, void*)
{
    if (!source[field::kInitialised])
        return 1;
    source[field::kInitialised] = 0;
    Virtual<uint32_t>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x9C), 0x38, source);
    Virtual<uint32_t>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x88), 0xC0, source + field::kBuildings);
    if (At<void*>(source, field::kMessageServer)) {
        for (const uint32_t message : kUnregistered)
            Virtual<uint8_t>(At<void*>(source, field::kMessageServer), 0x18, source + 4, message);
        Release(At<void*>(source, field::kMessageServer));
        At<void*>(source, field::kMessageServer) = nullptr;
    }
    if (void* const city = At<void*>(source, field::kCity)) {
        Release(city);
        At<void*>(source, field::kCity) = nullptr;
    }
    return 1;
}

// --- Messages (0x006AC5C0, this = +4) -------------------------------------------

uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const source = target - 4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x99EF1142:
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!Virtual<uint8_t>(Properties(occupant), 0xC, 0x029244DBu))
            Virtual<uint32_t>(source, type == 0x99EF1142 ? 0x34 : 0x38, occupant);
        return 1;
    }
    case 0x894671F6:
    case 0xE94671EB:
        if (Virtual<uint32_t>(message, 0x24) == At<uint32_t>(source, field::kKind))
            Virtual<uint32_t>(source, type == 0x894671F6 ? 0x44 : 0x3C, Virtual<uint32_t>(message, 0x38));
        return 1;
    case 0xE94671EC:
    case 0xE94671ED:
        if (Virtual<uint32_t>(message, 0x24) == At<uint32_t>(source, field::kKind))
            Virtual<uint32_t>(source, 0x40, Virtual<uint32_t>(message, 0x38), type == 0xE94671EC ? 1u : 0u);
        return 1;
    default:
        return 0;
    }
}

// --- Units ---------------------------------------------------------------------------

// 0x006DF760 (slot 3).
uint8_t __fastcall IsKind(char* source, void*, uint32_t kind)
{
    return kind == At<uint32_t>(source, field::kKind);
}

// 0x006DFB20 (slot 5): the buildings' units summed (the count set once).
int32_t __fastcall UnitCount(char* source, void*)
{
    int32_t total = 0;
    uint32_t count = 0;
    for (char* node = At<char*>(At<char*>(source, field::kBuildings), 0); node != At<char*>(source, field::kBuildings);
         node = At<char*>(node, 0)) {
        Virtual<uint8_t>(source, 0x1C, At<void*>(node, 8), nullptr, nullptr, &count);
        total += static_cast<int32_t>(count);
    }
    return total;
}

// 0x006AC9C0 (slot 4): units at all; busy when the dispatch service has as
// many out for this kind.
uint8_t __fastcall CanDispatch(char* source, void*, float, float, uint8_t* busy)
{
    const int32_t total = Virtual<int32_t>(source, 0x14);
    if (total <= 0)
        return 0;
    void* const service = Global<void*>(addr::SC4_DispatchService);
    if (!service)
        return 0;
    *busy = Virtual<uint32_t>(service, 0x2C, At<uint32_t>(source, field::kKind)) >= static_cast<uint32_t>(total);
    return 1;
}

// 0x006E0010 (slot 6).
uint32_t __fastcall GetBuildings(char* source, void*, void** list)
{
    using CopyFn = int32_t(__thiscall*)(void* self, void** out, void* in);
    return reinterpret_cast<CopyFn>(addr::SC4_CopyOccupantList)(source, list, source + field::kBuildings) > 0;
}

// 0x006DFB60 (slot 8): the nearest building with a unit home; a unit sent
// from it (slot 9).
void* __fastcall NearestBuilding(char* source, void*, float x, float z, uint32_t* slot)
{
    const float point[2] = {x, z};
    float best = 3.40282347e+38f;
    void* nearest = nullptr;
    for (char* node = At<char*>(At<char*>(source, field::kBuildings), 0); node != At<char*>(source, field::kBuildings);
         node = At<char*>(node, 0)) {
        void* const building = At<void*>(node, 8);
        uint32_t mask = 0;
        Virtual<uint8_t>(source, 0x1C, building, &mask, nullptr, nullptr);
        if (!mask)
            continue;
        float position[3];
        Virtual<uint32_t>(building, 0x20, position);
        const double dx = static_cast<double>(point[0]) - position[0];
        const double dz = static_cast<double>(point[1]) - position[2];
        const double distance = std::sqrt(dz * dz + dx * dx);
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = building;
        }
    }
    *slot = 0;
    if (nearest)
        Virtual<uint8_t>(source, 0x24, nearest, slot);
    return nearest;
}

// Slot 7: a building's units, the mask of those home (cut to that many
// bits) and its first dispatch slot, each if asked for.
uint8_t GetUnits(const Kind& kind, void* building, uint32_t* mask, uint32_t* first, uint32_t* count)
{
    void* const properties = Properties(building);
    uint32_t units = 0;
    if (!GetPropertyUint32(properties, kind.count, &units))
        return 0;
    if (count)
        *count = units;
    if (mask) {
        uint32_t bits = 0;
        if (!GetPropertyUint32(properties, kind.mask, &bits))
            return 0;
        *mask = bits & ((1u << (units & 31)) - 1);
    }
    if (first) {
        uint32_t slot = 0;
        if (!GetPropertyUint32(properties, kind.first, &slot))
            return 0;
        *first = slot;
    }
    return 1;
}

uint8_t __fastcall PlanesGetUnits(void*, void*, void* building, uint32_t* mask, uint32_t* first, uint32_t* count)
{
    return GetUnits(kPlanes, building, mask, first, count);
}

uint8_t __fastcall HelicoptersGetUnits(void*, void*, void* building, uint32_t* mask, uint32_t* first, uint32_t* count)
{
    return GetUnits(kHelicopters, building, mask, first, count);
}

// Slot 10: a unit back: its slot's bit set in the mask property, unless all
// are home or it already is. The mask lives where the building argument
// was, as in the base (protection_sim.cpp).
uint8_t ReturnUnit(const Kind& kind, char* source, void* building, uint32_t slot)
{
    uint32_t mask = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(building));
    uint32_t first = 0;
    uint32_t count = 0;
    Virtual<uint8_t>(source, 0x1C, building, &mask, &first, &count);
    uint32_t home = 0;
    for (uint32_t bit = 1; bit; bit <<= 1)
        if (mask & bit)
            ++home;
    if (home >= count)
        return 0;
    const uint32_t bit = 1u << ((slot - first) & 31);
    if (mask & bit)
        return 0;
    SetPropertyUint32(Properties(building), kind.mask, mask | bit);
    return 1;
}

uint8_t __fastcall PlanesReturnUnit(char* source, void*, void* building, uint32_t slot)
{
    return ReturnUnit(kPlanes, source, building, slot);
}

uint8_t __fastcall HelicoptersReturnUnit(char* source, void*, void* building, uint32_t slot)
{
    return ReturnUnit(kHelicopters, source, building, slot);
}

// Slot 9: a unit leaves: the given slot's bit (or for 0 the first unit at
// home) cleared; the slot written back.
uint8_t SendUnit(const Kind& kind, char* source, void* building, uint32_t* slot)
{
    uint32_t first = 0;
    uint32_t mask = 0;
    uint32_t count = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(building));
    Virtual<uint8_t>(source, 0x1C, building, &mask, &first, &count);
    uint32_t offset = 0;
    uint32_t bit;
    if (*slot != 0) {
        offset = *slot - first;
        if (offset > count)
            return 0;
        bit = 1u << (offset & 31);
    } else if (count == 0) {
        bit = 0;
    } else {
        do {
            bit = 1u << (offset & 31);
            if (mask & bit)
                break;
        } while (++offset < count);
    }
    SetPropertyUint32(Properties(building), kind.mask, mask & ~bit);
    *slot = first + offset;
    return 1;
}

uint8_t __fastcall PlanesSendUnit(char* source, void*, void* building, uint32_t* slot)
{
    return SendUnit(kPlanes, source, building, slot);
}

uint8_t __fastcall HelicoptersSendUnit(char* source, void*, void* building, uint32_t* slot)
{
    return SendUnit(kHelicopters, source, building, slot);
}

// Slot 11.
uint8_t __fastcall PlanesAppearance(void*, void*, uint32_t, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t)
{
    *a = 0x4303;
    b[0] = b[1] = b[2] = 0;
    *c = 0x11882;
    return 1;
}

uint8_t __fastcall HelicoptersAppearance(void*, void*, uint32_t, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t)
{
    *a = 0x4301;
    b[0] = b[1] = b[2] = 0;
    *c = 0x11882;
    return 1;
}

// Slot 17: a unit back from the dispatch service's entry.
void OnDispatch(const Kind& kind, char* source, uint32_t id)
{
    void* const service = Global<void*>(addr::SC4_DispatchService);
    if (!service)
        return;
    uint8_t info[0x2C] = {};
    if (Virtual<uint8_t>(service, 0x28, id, info))
        ReturnUnit(kind, source, At<void*>(info, 0x20), At<uint32_t>(info, 8));
}

void __fastcall PlanesOnDispatch(char* source, void*, uint32_t id)
{
    OnDispatch(kPlanes, source, id);
}

void __fastcall HelicoptersOnDispatch(char* source, void*, uint32_t id)
{
    OnDispatch(kHelicopters, source, id);
}

// 0x006ACAC0 (slot 16): the entry read and dropped.
void __fastcall OnDispatchMove(void*, void*, uint32_t id, uint32_t)
{
    if (void* const service = Global<void*>(addr::SC4_DispatchService)) {
        uint8_t info[0x2C];
        Virtual<uint8_t>(service, 0x28, id, info);
    }
}

// --- Buildings -------------------------------------------------------------------

// The game's std::vector<bool>.
struct BitVector {
    uint32_t* start;
    int32_t startBit;
    uint32_t* end;
    int32_t endBit;
    uint32_t* capacity;
    uint32_t* WordOf(int32_t index, uint32_t* bit) const
    {
        const int32_t at = startBit + index;
        *bit = 1u << (at % 32);
        return start + at / 32;
    }
};

// Slot 13: a building with aircraft joins the list (held), all its units
// home, and takes the first run of free dispatch slots after slot 0.
void OnBuildingAdded(const Kind& kind, char* source, void* building)
{
    using NodesFn = char*(__thiscall*)(void* list, uint32_t count, uint32_t unused);
    using ResizeFn = void(__thiscall*)(BitVector* bits, uint32_t count, uint32_t value);
    if (Virtual<uint32_t>(building, 0x1C) != kBuildingType)
        return;
    int32_t aircraft = 0;
    GetPropertySint32(Properties(building), kind.count, &aircraft);
    if (aircraft <= 0)
        return;
    uint32_t count = 0;
    Virtual<uint32_t>(building, 0x4); // AddRef
    char* const head = At<char*>(source, field::kBuildings);
    char* const node = reinterpret_cast<NodesFn>(addr::SC4List_AllocateNodes)(source + field::kBuildings, 1, 0);
    if (node + 8)
        At<void*>(node, 8) = building;
    char* const last = At<char*>(head, 4);
    At<char*>(node, 4) = last;
    At<char*>(node, 0) = head;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
    Virtual<uint8_t>(source, 0x1C, building, nullptr, nullptr, &count);
    SetPropertyUint32(Properties(building), kind.mask, (1u << (count & 31)) - 1);
    BitVector taken{};
    reinterpret_cast<ResizeFn>(addr::SC4BitVector_Resize)(&taken, At<uint32_t>(source, field::kSlots), 0);
    for (char* at = At<char*>(At<char*>(source, field::kBuildings), 0); at != At<char*>(source, field::kBuildings);
         at = At<char*>(at, 0)) {
        void* const other = At<void*>(at, 8);
        if (other == building)
            continue;
        uint32_t first = 0, slots = 0;
        Virtual<uint8_t>(source, 0x1C, other, nullptr, &first, &slots);
        for (uint32_t i = 0; i < slots; ++i) {
            uint32_t bit;
            *taken.WordOf(static_cast<int32_t>(first + i), &bit) |= bit;
        }
    }
    uint32_t start = 1;
    while (start < At<uint32_t>(source, field::kSlots)) {
        uint32_t clash = 0;
        bool found = false;
        for (; clash < count; ++clash) {
            uint32_t bit;
            if (*taken.WordOf(static_cast<int32_t>(clash + start), &bit) & bit) {
                found = true;
                break;
            }
        }
        if (!found || clash >= count)
            break;
        start += clash + 1;
    }
    SetPropertyUint32(Properties(building), kind.first, start);
    if (At<uint32_t>(source, field::kSlots) < start + count)
        At<uint32_t>(source, field::kSlots) = start + count;
    if (taken.start)
        Free(taken.start);
}

void __fastcall PlanesOnBuildingAdded(char* source, void*, void* building)
{
    OnBuildingAdded(kPlanes, source, building);
}

void __fastcall HelicoptersOnBuildingAdded(char* source, void*, void* building)
{
    OnBuildingAdded(kHelicopters, source, building);
}

// 0x006ACB60 (slot 14): a listed building leaves, released.
void __fastcall OnBuildingRemoved(char* source, void*, void* building)
{
    using EraseFn = void(__thiscall*)(void* list, char** next, char* node);
    if (Virtual<uint32_t>(building, 0x1C) != kBuildingType)
        return;
    char* const head = At<char*>(source, field::kBuildings);
    char* node = At<char*>(head, 0);
    while (node != head && At<void*>(node, 8) != building)
        node = At<char*>(node, 0);
    if (node == At<char*>(source, field::kBuildings))
        return;
    char* next = nullptr;
    reinterpret_cast<EraseFn>(addr::SC4List_Erase)(source + field::kBuildings, &next, node);
    Release(building);
}

// --- Write (0x006AC930) and Read, this = +8 ----------------------------------------

uint32_t StreamOk(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

uint32_t __fastcall Write(char* serializable, void*, void* stream)
{
    using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
    char* const source = serializable - 8;
    Virtual<uint8_t>(stream, 0x1C, uint16_t{1});
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReferenceList)(stream, source + field::kBuildings);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(source, field::kSlots));
    return StreamOk(stream);
}

// Planes accept version 1 only, helicopters up to 1.
uint32_t Read(char* serializable, void* stream, bool exactly)
{
    using ReadObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
    char* const source = serializable - 8;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (exactly ? version != 1 : version > 1)
        return 0;
    reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReferenceList)(stream, source + field::kBuildings);
    Virtual<uint8_t>(stream, 0x24, source + field::kSlots);
    return StreamOk(stream);
}

uint32_t __fastcall PlanesRead(char* serializable, void*, void* stream)
{
    return Read(serializable, stream, true);
}

uint32_t __fastcall HelicoptersRead(char* serializable, void*, void* stream)
{
    return Read(serializable, stream, false);
}

// Hooks to leave out, for bisecting: OPENSC4_SOURCES_SKIP lists function
// addresses (hex).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_SOURCES_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4DispatchSource_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4DispatchSource_Release, reinterpret_cast<void*>(&ReleaseSelf)},
        {addr::SC4DispatchSource_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4DispatchSource_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4DispatchSource_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4DispatchSource_IsKind, reinterpret_cast<void*>(&IsKind)},
        {addr::SC4DispatchSource_UnitCount, reinterpret_cast<void*>(&UnitCount)},
        {addr::SC4DispatchSource_CanDispatch, reinterpret_cast<void*>(&CanDispatch)},
        {addr::SC4DispatchSource_GetBuildings, reinterpret_cast<void*>(&GetBuildings)},
        {addr::SC4DispatchSource_NearestBuilding, reinterpret_cast<void*>(&NearestBuilding)},
        {addr::SC4DispatchSource_OnBuildingRemoved, reinterpret_cast<void*>(&OnBuildingRemoved)},
        {addr::SC4DispatchSource_OnDispatchMove, reinterpret_cast<void*>(&OnDispatchMove)},
        {addr::SC4DispatchSource_Write, reinterpret_cast<void*>(&Write)},
        {addr::SC4FirePlaneSource_GetUnits, reinterpret_cast<void*>(&PlanesGetUnits)},
        {addr::SC4FirePlaneSource_ReturnUnit, reinterpret_cast<void*>(&PlanesReturnUnit)},
        {addr::SC4FirePlaneSource_SendUnit, reinterpret_cast<void*>(&PlanesSendUnit)},
        {addr::SC4FirePlaneSource_GetUnitAppearance, reinterpret_cast<void*>(&PlanesAppearance)},
        {addr::SC4FirePlaneSource_OnBuildingAdded, reinterpret_cast<void*>(&PlanesOnBuildingAdded)},
        {addr::SC4FirePlaneSource_OnDispatch, reinterpret_cast<void*>(&PlanesOnDispatch)},
        {addr::SC4FirePlaneSource_Read, reinterpret_cast<void*>(&PlanesRead)},
        {addr::SC4FirePlaneSource_GetGZCLSID, reinterpret_cast<void*>(&PlanesClass)},
        {addr::SC4PoliceHelicopterSource_GetUnits, reinterpret_cast<void*>(&HelicoptersGetUnits)},
        {addr::SC4PoliceHelicopterSource_ReturnUnit, reinterpret_cast<void*>(&HelicoptersReturnUnit)},
        {addr::SC4PoliceHelicopterSource_SendUnit, reinterpret_cast<void*>(&HelicoptersSendUnit)},
        {addr::SC4PoliceHelicopterSource_GetUnitAppearance, reinterpret_cast<void*>(&HelicoptersAppearance)},
        {addr::SC4PoliceHelicopterSource_OnBuildingAdded, reinterpret_cast<void*>(&HelicoptersOnBuildingAdded)},
        {addr::SC4PoliceHelicopterSource_OnDispatch, reinterpret_cast<void*>(&HelicoptersOnDispatch)},
        {addr::SC4PoliceHelicopterSource_Read, reinterpret_cast<void*>(&HelicoptersRead)},
        {addr::SC4PoliceHelicopterSource_GetGZCLSID, reinterpret_cast<void*>(&HelicoptersClass)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        if (Skipped(hooks[i].address))
            continue;
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "simulators.protection.sources",
    "simulators/protection",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4DispatchSource_Init,
    "Our fire plane and police helicopter dispatch sources (docs/fire-police.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::protection_sources
