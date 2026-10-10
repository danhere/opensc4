// Our cSC4Lot, the capacity part (build 641): the functions with their entry
// in 0x006C09E0-0x006C2340 that are not the object part's. Population and
// travel population, occupancy, the occupant wealth, road access and the
// small getters and setters of the primary interface; the business unit's
// Read, factory and deleting destructor; the trip list's node helpers.
// The class map, the vtables and the field list are in citylot_sim.h.
//
// Ours (address, what it is, how it is installed):
//   slots of the primary vtable 0x00AB19B0, swapped:
//     3 AsPropertyHolder 0x006C2040, 4 AsTrafficSource 0x006C2050 (both code
//     shared with other classes: only the lot's slots are swapped),
//     6 IsInitialized 0x006C2020, 12 GetFlags 0x006C2030, 16 GetAltitude
//     0x006C2060, 18 GetSlope 0x006C2070, 19 GetSlopeAtTimeOfConstruction
//     0x006C2080, 23 GetFacing 0x006C2090, 24 SetFacing 0x006C20A0, 26
//     SetUnit 0x006C20B0, 28 HasRoadAccess 0x006C20E0, 29
//     HasOrganicRoadAccess 0x006C1D10, 30 UpdateRoadAccess 0x006C1DA0, 31
//     GetZoneType 0x006C20F0, 33 GetState 0x006C2100, 35 GetCondition
//     0x006C2110, 37 IsInLivableState 0x006C2150, 38 HasCapacity 0x006C2160,
//     39 IsInDemolishableState 0x006C2180, 41 GetOccupantWealth 0x006C21F0,
//     43 IsHistorical 0x006C2220, 44 SetHistorical 0x006C2230, 45
//     GetVacateReason 0x006C2250, 47 SetBuildingType 0x006C2200, 54
//     GetPopulations 0x006C21E0, 55 SetPopulation 0x006C1940, 58
//     AddTravelPopulation 0x006C0BA0, 61 GetOccupancy 0x006C0CF0, 62
//     GetOccupancyByType 0x006C10F0, 65 GetNeedsPower 0x006C21A0, 66
//     GetHasPower 0x006C21B0, 67 SetHasPower 0x006C21C0, 68
//     GetVerticesAreLocked 0x006C2120, 69 SetVerticesAreLocked 0x006C2130
//   slot 5 of the traffic source part's vtable 0x00AB1914: the cell
//     rectangle 0x006C22D0
//   the business unit: Read 0x006C09E0 (slot 4 of its +0x4 vtable
//     0x00AB18C4, and slot 24 of its holder vtable 0x00AB1860, the thunk
//     0x006C0B90), swapped; Create 0x006C0EB0 (the class factory, registered
//     at 0x0046046C) and the deleting destructor 0x006C0ED0 (reached through
//     the holder's slot 22, thunk 0x006C0870), detours
//   detours for callers in other parts: UpdateOccupantWealth 0x006C1430
//     (SetState, 0x006C33B5), the trip record's copy constructor 0x006C0AE0
//     (0x006C2CF3), the trip list's push front 0x006C1E10 (0x006C2B60) and
//     erase 0x006C0FA0 (0x006C2D75). Their call sites keep nothing in
//     ECX/EDX/EAX across the call and pass nothing in registers but `this`.
//   plain functions (only called from here): the lot's own road access
//     0x006C1A30, the transit lot's 0x006C15D0, the access through
//     neighbours 0x006C17E0 (Ghidra splits it at 0x006C1808).
// Left the game's (container code): map<int32, uint8>::operator[]
// 0x006C0F30 and the trip list's clear 0x006C0FF0 (both also called by other
// classes), the 12-byte map vector helpers 0x006C0B40, 0x006C1040, 0x006C1070
// (only called by the state part's 0x006C2E70) and 0x006C10A0 (shared).
//
// Fields used here (citylot_sim.h has the full list; evidence in brackets):
//   0x20-0x23 uint8 cell rectangle min x, min z, max x, max z [0x006C22D0
//             widens them into four int32; 0x006C1A30 and 0x006C15D0 hand
//             them to the lot manager and the occupant manager]
//   0x24, 0x28, 0x2C float altitude, slope, slope when built [slots 16, 18,
//             19 load them on the FPU stack]
//   0x3C      uint32 building type [slot 47 stores it and zeroes +0x82]
//   0x40      map<int32 purpose, uint8>: road access by purpose, key 0 the
//             lot's own access [0x006C1A30 stores key 0; slot 29 looks up 0,
//             then its argument; 0x006C17E0 stores its argument's entry;
//             slot 30 recomputes every non-zero key]
//   0x4C      cISC4BusinessUnit*, referenced [slot 26: Release the old,
//             AddRef the new]
//   0x54      vector<map<uint32 developer type, uint16>> populations, one
//             map per entry of the building profile's capacity vector [slots
//             54, 55, 61, 62]
//   0x60      map<uint32 developer type, uint16> travel population [slot 58]
//   0x6C      float[3] travel jobs [slot 58 adds the other lot's GetJobs
//             floats 1-3]
//   0x78      uint8 initialized [slot 6]
//   0x79      uint8 flags: 0x08 needs power, 0x10 has power, 0x20
//             historical, 0x40 vertices locked [slots 12, 43, 44, 65-69]
//   0x7E      uint8 facing [slots 23, 24; 0x006C1A30 looks only that way for
//             zone types 1-6]
//   0x7F      uint8 state [slots 33, 37 (== 3), 38 (2-3), 39 (2-4 or 6)]
//   0x80      uint8 condition [slot 35]
//   0x81      uint8 zone type [slot 31]
//   0x82      uint8 occupant wealth [slot 41; 0x006C1430]
//   0x83      uint8 vacate reason [slot 45]
// The building profile (the building development simulator's slot 39 for a
// building type): +0x0 uint32 purpose, +0x4 uint32 highest wealth, +0xC
// vector<map<uint32 developer type, uint16 capacity>>.
// The business unit, as its Read sees it (`this` = unit + 4): +0x18 the
// member list's head (unit + 0x1C); nodes next, previous, lot (referenced).
// A trip record (0x2C bytes): two byte vectors (+0x0, +0xC), then +0x18
// uint32, +0x1C four uint8, +0x20 two uint16, +0x24 and +0x28 uint32.
//
// Virtual calls with arguments: each callee's argument count is taken from
// the callee's ret N (va-tools vt.py): the lot's own slots (table in
// citylot_sim.h); the building development simulator 0x00AB0AF8 slots 30 (2)
// and 39 (1); the lot manager 0x00AB37C0 slots 9 (3) and 12 (3); the occupant
// manager 0x00A8F7F0 slots 41 (5) and 48 (1); the city 0x00A87620 slot 95
// (4); the traffic network map 0x00A8FED8 slot 8 (4); the node allocator
// 0x00AD1E40 slots 3 (1) and 4 (1); the stream 0x00AA2280 slots 7 (1), 9 (1)
// and 18 (0); occupants (57 vtables) slots 0 (2), 8 (1) and 22 (1); network
// occupants (IID 0xA821EF94, 7 vtables) slots 8 (1) and 9 (1); prop
// occupants slot 7 (0); the building occupant 0x00A872C0 slot 3 (0).
#include "core/citylot_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::citylot_sim {
namespace {

#define OPENSC4_CITYLOT_FIRST(name)                                                                                    \
    do {                                                                                                               \
        static bool logged = false;                                                                                    \
        if (!logged)                                                                                                   \
            FirstCall(name, logged);                                                                                   \
    } while (0)

// --- The game's helpers ---------------------------------------------------------

using AllocateFn = void*(__cdecl*)(std::size_t);
using FreeFn = void(__cdecl*)(void*);
using IndexUint16Fn = uint16_t*(__thiscall*)(void* map, const uint32_t* key);
using IndexFloatFn = float*(__thiscall*)(void* map, const uint32_t* key);
using AtByteFn = uint8_t*(__thiscall*)(void* map, const int32_t* key);
using FindSignedFn = void(__thiscall*)(void* map, char** out, const int32_t* key);
using EraseSubtreeFn = void(__thiscall*)(void* map, char* node);
using InsertFn = void(__thiscall*)(void* vector, void* where, const int32_t* value, void* allocator, uint32_t count,
                                   uint32_t atEnd);
using ThisFn = void(__thiscall*)(void* self);
using CopyVectorFn = void*(__thiscall*)(void* self, const void* source);
using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
using ConstructFn = void*(__thiscall*)(void* self);

void* Allocate(std::size_t size) { return reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size); }
void Free(void* memory) { reinterpret_cast<FreeFn>(addr::SC4_Free)(memory); }

uint16_t* IndexUint16(void* map, const uint32_t* key)
{
    return reinterpret_cast<IndexUint16Fn>(addr::SC4Uint32Uint16Map_Index)(map, key);
}

float* IndexFloat(void* map, const uint32_t* key)
{
    return reinterpret_cast<IndexFloatFn>(addr::SC4Uint32FloatMap_Index)(map, key);
}

// The road access map's operator[] (0x006C0F30): the flag's address,
// inserting 0 when the purpose has no entry.
uint8_t* AccessAt(char* self, const int32_t* purpose)
{
    return reinterpret_cast<AtByteFn>(addr::SC4Sint32ByteMap_At)(self + field::kMap40, purpose);
}

void AddRef(void* object) { Virtual<uint32_t>(object, 0x4); }
void Release(void* object) { Virtual<uint32_t>(object, 0x8); }

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// find() of a map with uint32 keys, as the game inlines it: the node, or the
// head for none.
char* FindUint32(char* head, uint32_t key)
{
    char* best = head;
    char* node = At<char*>(head, 0x4);
    while (node) {
        if (At<uint32_t>(node, 0x10) < key) {
            node = At<char*>(node, 0xC);
        } else {
            best = node;
            node = At<char*>(node, 0x8);
        }
    }
    if (best == head || key < At<uint32_t>(best, 0x10))
        return head;
    return best;
}

// The number of 12-byte maps in a vector (begin, end), as the game computes
// it (a signed division).
uint32_t MapCount(char* vector)
{
    return static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, 0x4) - At<char*>(vector, 0x0)) / 0xC);
}

// The building profile of a building type (the building development
// simulator's slot 39).
char* BuildingProfile(uint32_t buildingType)
{
    return Virtual<char*>(Global<void*>(addr::SC4_Service43D68), 0x9C, buildingType);
}

// A map as the game's stack holds one (0xC bytes): head, size, unused.
struct LocalMap {
    char* head;
    uint32_t size;
    uint32_t unused;
};

// --- Occupant wealth -------------------------------------------------------------

// 0x006C1430: the occupant wealth (+0x82) from the occupancy by developer
// type. Without a building type (not forced) the wealth is 0; without a
// purpose in the profile nothing changes. Otherwise: a lot without a wealth,
// or whose wealth's developer type has no occupancy, starts from the
// profile's highest wealth; then, from the highest wealth down, each wealth
// whose developer type has an occupancy above the best so far (which starts
// at 0) becomes the wealth.
void __fastcall UpdateOccupantWealth(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("UpdateOccupantWealth");
    const uint32_t buildingType = Virtual<uint32_t>(self, slot::kGetBuildingType, 0u);
    if (!buildingType) {
        At<uint8_t>(self, field::kWealth) = 0;
        return;
    }
    void* const development = Global<void*>(addr::SC4_Service43D68);
    char* const profile = Virtual<char*>(development, 0x9C, buildingType);
    if (At<uint32_t>(profile, 0x0) == 0)
        return;

    LocalMap occupancy = {nullptr, 0, 0};
    occupancy.head = static_cast<char*>(Allocate(0x18));
    occupancy.size = 0;
    At<uint8_t>(occupancy.head, 0x0) = 0;
    At<char*>(occupancy.head, 0x4) = nullptr;
    At<char*>(occupancy.head, 0x8) = occupancy.head;
    At<char*>(occupancy.head, 0xC) = occupancy.head;
    Virtual<void>(self, slot::kGetOccupancyByType, &occupancy);

    const uint8_t wealth = At<uint8_t>(self, field::kWealth);
    if (wealth == 0) {
        At<uint8_t>(self, field::kWealth) = At<uint8_t>(profile, 0x4);
    } else {
        const uint32_t type =
            Virtual<uint32_t>(development, 0x78, At<uint32_t>(profile, 0x0), static_cast<uint32_t>(wealth));
        if (FindUint32(occupancy.head, type) == occupancy.head)
            At<uint8_t>(self, field::kWealth) = At<uint8_t>(profile, 0x4);
    }

    float best = Global<float>(addr::cSC4Lot_ZeroFloat);
    for (int32_t candidate = At<int32_t>(profile, 0x4); candidate >= 1; --candidate) {
        uint32_t type = Virtual<uint32_t>(development, 0x78, At<uint32_t>(profile, 0x0), candidate);
        if (FindUint32(occupancy.head, type) == occupancy.head)
            continue;
        const float value = *IndexFloat(&occupancy, &type);
        if (value > best) {
            best = value;
            At<uint8_t>(self, field::kWealth) = static_cast<uint8_t>(candidate);
        }
    }

    if (occupancy.size != 0) {
        reinterpret_cast<EraseSubtreeFn>(addr::SC4Uint32FloatMap_EraseSubtree)(&occupancy,
                                                                               At<char*>(occupancy.head, 0x4));
        At<char*>(occupancy.head, 0x8) = occupancy.head;
        At<char*>(occupancy.head, 0x4) = nullptr;
        At<char*>(occupancy.head, 0xC) = occupancy.head;
        occupancy.size = 0;
    }
    if (occupancy.head)
        Free(occupancy.head);
}

// --- Population --------------------------------------------------------------------

// Slot 55 (0x006C1940): a developer type's population, capped at the
// available capacity (slot 52, not forced; compared as uint16). Stored in the
// populations map that matches the first map of the building profile's
// capacities holding the type; then the occupant wealth is updated. False
// (AL) when no capacity map has the type.
uint8_t __fastcall SetPopulation(char* self, void*, uint32_t type, uint32_t population)
{
    OPENSC4_CITYLOT_FIRST("SetPopulation");
    const uint32_t available = Virtual<uint32_t>(self, slot::kGetAvailableCapacity, type, 0u);
    if (static_cast<uint16_t>(population) > static_cast<uint16_t>(available))
        population = available;
    char* const profile = BuildingProfile(Virtual<uint32_t>(self, slot::kGetBuildingType, 1u));
    char* map = At<char*>(profile, 0xC);
    const uint32_t count = MapCount(profile + 0xC);
    for (uint32_t i = 0; i < count; ++i, map += 0xC) {
        char* const head = At<char*>(map, 0x0);
        if (FindUint32(head, type) == head)
            continue;
        uint32_t key = type;
        *IndexUint16(At<char*>(self, field::kPopulations) + i * 0xC, &key) = static_cast<uint16_t>(population);
        UpdateOccupantWealth(self, nullptr);
        return 1;
    }
    return 0;
}

// Slot 58 (0x006C0BA0): adds another lot's populations (every map of its
// slot 54 vector) to the travel population by developer type and its jobs
// (slot 64: floats 1-3 of the four) to the travel jobs, then calls the object
// part's 0x006BF6A0 (the traffic simulator is told of the new totals).
void __fastcall AddTravelPopulation(char* self, void*, void* other)
{
    OPENSC4_CITYLOT_FIRST("AddTravelPopulation");
    char* const populations = Virtual<char*>(other, slot::kGetPopulations);
    for (char* map = At<char*>(populations, 0x0); map != At<char*>(populations, 0x4); map += 0xC) {
        for (char* node = At<char*>(At<char*>(map, 0x0), 0x8); node != At<char*>(map, 0x0); node = TreeNext(node)) {
            uint32_t type = At<uint32_t>(node, 0x10);
            const uint16_t people = At<uint16_t>(node, 0x14);
            char* const head = At<char*>(self, field::kTravel);
            if (FindUint32(head, type) == head)
                *IndexUint16(self + field::kTravel, &type) = people;
            else
                *IndexUint16(self + field::kTravel, &type) += people;
        }
    }
    float jobs[4] = {}; // the game leaves the first of the four as its stack had it and does not read it
    Virtual<void>(other, slot::kGetJobs, jobs);
    for (int i = 0; i < 3; ++i) {
        float& total = At<float>(self, field::kTravelJobs + i * sizeof(float));
        total = static_cast<float>(static_cast<double>(jobs[i + 1]) + static_cast<double>(total));
    }
    reinterpret_cast<ThisFn>(addr::cSC4Lot_PushTravelJobs)(self);
}

// --- Occupancy ---------------------------------------------------------------------

// Slot 61 (0x006C0CF0): the lot's occupancy into *out; returns out. For each
// capacity map of the building profile: the sum over its developer types of
// population / capacity, times the map's average capacity (the integer
// capacity sum / the number of types), added to a truncated running total;
// the result is that total over the sum of the average capacities. 0 (the
// float at 0x00B49114) without a building type, without maps or when the sum
// is not positive. Looking a population up inserts a 0 entry when the type
// has none (the game's operator[]). A capacity map without entries divides by
// zero, as in the game.
float* __fastcall GetOccupancy(char* self, void*, float* out)
{
    OPENSC4_CITYLOT_FIRST("GetOccupancy");
    const uint32_t buildingType = Virtual<uint32_t>(self, slot::kGetBuildingType, 1u);
    if (buildingType) {
        char* const capacities = BuildingProfile(buildingType) + 0xC;
        int32_t weighted = 0;
        int32_t total = 0;
        if (MapCount(capacities) != 0) {
            uint32_t i = 0;
            do {
                char* const map = At<char*>(capacities, 0x0) + i * 0xC;
                float sum = 0.0f;
                int32_t types = 0;
                int32_t capacity = 0;
                for (char* node = At<char*>(At<char*>(map, 0x0), 0x8); node != At<char*>(map, 0x0);
                     node = TreeNext(node)) {
                    uint32_t type = At<uint32_t>(node, 0x10);
                    const uint16_t typeCapacity = At<uint16_t>(node, 0x14);
                    const uint16_t people = *IndexUint16(At<char*>(self, field::kPopulations) + i * 0xC, &type);
                    sum = static_cast<float>(static_cast<double>(people) / static_cast<double>(typeCapacity) +
                                             static_cast<double>(sum));
                    capacity += typeCapacity;
                    ++types;
                }
                const int32_t average = capacity / types;
                weighted = Ftol(static_cast<double>(average) * static_cast<double>(sum) +
                                static_cast<double>(weighted));
                total += average;
                ++i;
            } while (i < MapCount(capacities));
            if (total > 0) {
                *out = static_cast<float>(static_cast<double>(weighted) / static_cast<double>(total));
                return out;
            }
        }
    }
    At<uint32_t>(out, 0) = Global<uint32_t>(addr::cSC4Lot_ZeroFloat);
    return out;
}

// Slot 62 (0x006C10F0): the occupancy by developer type into a map<uint32,
// float>, emptied first. Each capacity map of the building profile gets its
// average capacity (integer); a type's share is (population / capacity) *
// (its map's average / the sum of the averages, as a float), added up when a
// type is in several maps. Nothing when the sum is not positive.
void __fastcall GetOccupancyByType(char* self, void*, char* out)
{
    OPENSC4_CITYLOT_FIRST("GetOccupancyByType");
    if (At<uint32_t>(out, 0x4) != 0) {
        reinterpret_cast<EraseSubtreeFn>(addr::SC4Uint32FloatMap_EraseSubtree)(out,
                                                                               At<char*>(At<char*>(out, 0x0), 0x4));
        At<char*>(At<char*>(out, 0x0), 0x8) = At<char*>(out, 0x0);
        At<char*>(At<char*>(out, 0x0), 0x4) = nullptr;
        At<char*>(At<char*>(out, 0x0), 0xC) = At<char*>(out, 0x0);
        At<uint32_t>(out, 0x4) = 0;
    }
    int32_t total = 0;
    int32_t* averages[3] = {nullptr, nullptr, nullptr}; // a vector<int32>: begin, end, capacity
    char* const capacities = BuildingProfile(Virtual<uint32_t>(self, slot::kGetBuildingType, 1u)) + 0xC;
    if (MapCount(capacities) == 0)
        return;

    uint32_t i = 0;
    do {
        if (averages[1] != averages[2]) {
            if (averages[1])
                *averages[1] = 0;
            ++averages[1];
        } else {
            const int32_t zero = 0;
            uint8_t allocator = 0;
            reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(averages, averages[1], &zero, &allocator, 1, 1);
        }
        char* const map = At<char*>(capacities, 0x0) + i * 0xC;
        int32_t types = 0;
        for (char* node = At<char*>(At<char*>(map, 0x0), 0x8); node != At<char*>(map, 0x0); node = TreeNext(node)) {
            uint32_t type = At<uint32_t>(node, 0x10);
            const uint16_t typeCapacity = At<uint16_t>(node, 0x14);
            IndexUint16(At<char*>(self, field::kPopulations) + i * 0xC, &type); // only for its insertion
            averages[0][i] += typeCapacity;
            ++types;
        }
        averages[0][i] = averages[0][i] / types; // a map without entries divides by zero, as in the game
        total += averages[0][i];
        ++i;
    } while (i < MapCount(capacities));

    if (total > 0 && MapCount(capacities) != 0) {
        uint32_t j = 0;
        do {
            char* const map = At<char*>(capacities, 0x0) + j * 0xC;
            if (At<char*>(At<char*>(map, 0x0), 0x8) != At<char*>(map, 0x0)) {
                const float floatTotal = static_cast<float>(total);
                for (char* node = At<char*>(At<char*>(map, 0x0), 0x8); node != At<char*>(map, 0x0);
                     node = TreeNext(node)) {
                    uint32_t type = At<uint32_t>(node, 0x10);
                    const uint16_t typeCapacity = At<uint16_t>(node, 0x14);
                    const uint16_t people = *IndexUint16(At<char*>(self, field::kPopulations) + j * 0xC, &type);
                    const double filled = static_cast<double>(people) / static_cast<double>(typeCapacity);
                    const double weight = static_cast<double>(averages[0][j]) / static_cast<double>(floatTotal);
                    const float share = static_cast<float>(filled * weight);
                    char* const head = At<char*>(out, 0x0);
                    if (FindUint32(head, type) != head) {
                        float* const value = IndexFloat(out, &type);
                        *value = static_cast<float>(static_cast<double>(share) + static_cast<double>(*value));
                    } else {
                        *IndexFloat(out, &type) = share;
                    }
                }
            }
            ++j;
        } while (j < MapCount(capacities));
    }
    if (averages[0])
        Free(averages[0]);
}

// --- Road access -------------------------------------------------------------------

constexpr int32_t kNeighbourX[4] = {-1, 0, 1, 0}; // the table at 0x00AB18F4
constexpr int32_t kNeighbourZ[4] = {0, -1, 0, 1}; // the table at 0x00AB1904

// A new empty list of the node allocator at 0x00B207E4: its sentinel.
char* NewList()
{
    char* const head = Virtual<char*>(ListNodeAllocator(), 0xC, 0xCu);
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    return head;
}

// Frees such a list's nodes (SC4List::Clear2), then its sentinel.
void DeleteList(char** list)
{
    reinterpret_cast<ThisFn>(addr::SC4List_Clear2)(list);
    char* const head = *list;
    Virtual<void>(ListNodeAllocator(), 0x10, head);
}

// 0x006C15D0: road access of a lot whose building is in occupant group
// 0x1304. Starts true; each prop occupant (filter 0xA823821E) of type
// 0x2C520000 or 0x2C530000 in the lot's rectangle sets it to whether one of
// the four cells around the prop's cell has a network of mask 0x449 (the
// traffic network map's slot 8) for which the network's slot 8 with 0x200000
// is false. The last such prop decides. The occupants are released one by
// one as they are looked at (the list is not given back to the occupant
// manager).
uint8_t TransitRoadAccess(char* self)
{
    OPENSC4_CITYLOT_FIRST("TransitRoadAccess");
    uint8_t result = 1;
    char* list = NewList();
    int32_t first[2] = {At<uint8_t>(self, field::kMinX), At<uint8_t>(self, field::kMinZ)};
    int32_t last[2] = {At<uint8_t>(self, field::kMaxX), At<uint8_t>(self, field::kMaxZ)};
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA4, &list, first, last, 0xA823821Eu, 0xFFFFFFFFu);
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        void* prop = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0xE9793A65u, &prop)) {
            const uint32_t type = Virtual<uint32_t>(prop, 0x1C);
            if (type == 0x2C520000 || type == 0x2C530000) {
                uint32_t position[3] = {}; // three floats; the game's are not initialised
                Virtual<void>(occupant, 0x20, position);
                int32_t cellX = 0; // the game's are not initialised
                int32_t cellZ = 0;
                Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
                void* const networkMap = Global<void*>(addr::SC4_Service43D54);
                result = 0;
                for (int i = 0; i < 4; ++i) {
                    void* const network = Virtual<void*>(networkMap, 0x20, kNeighbourX[i] + cellX,
                                                         kNeighbourZ[i] + cellZ, 0x449u, 1u);
                    if (network && !Virtual<uint8_t>(network, 0x20, 0x200000u))
                        result = 1;
                }
            }
        }
        Release(occupant);
        if (prop)
            Release(prop);
    }
    DeleteList(&list);
    return result;
}

// 0x006C1A30: the lot's own road access, stored under purpose 0 of the
// access map (not for a building of occupant group 0x1304, which gets
// TransitRoadAccess uncached). For each direction (only the facing for zone
// types 1-6, else 0-3): the lot manager's slot 12 gives the cells along that
// side of the lot's rectangle; access is true when a network occupant
// (filter 0xC772BF98) there has a network of mask 0x449 (its slot 9).
uint8_t ComputeRoadAccess(char* self)
{
    OPENSC4_CITYLOT_FIRST("ComputeRoadAccess");
    if (void* const building = Virtual<void*>(self, slot::kGetBuilding)) {
        void* const occupant = Virtual<void*>(building, 0xC);
        if (Virtual<uint8_t>(occupant, 0x58, 0x1304u))
            return TransitRoadAccess(self);
    }
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    const uint8_t zone = At<uint8_t>(self, field::kZoneType);
    int32_t direction = 0;
    int32_t lastDirection = 3;
    if (zone > 0 && zone <= 6) {
        direction = At<uint8_t>(self, field::kFacing);
        lastDirection = direction;
    }
    for (; direction <= lastDirection; ++direction) {
        int32_t rect[4] = {At<uint8_t>(self, field::kMinX), At<uint8_t>(self, field::kMinZ),
                           At<uint8_t>(self, field::kMaxX), At<uint8_t>(self, field::kMaxZ)};
        int32_t side[4] = {}; // the game's is not initialised
        Virtual<void>(lotManager, 0x30, rect, 1u << (direction & 31), side);
        int32_t first[2] = {side[0], side[1]};
        int32_t last[2] = {side[2], side[3]};
        char* list = NewList();
        Virtual<void>(occupants, 0xA4, &list, first, last, 0xC772BF98u, 0xFFFFFFFFu);
        for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
            void* network = nullptr;
            if (Virtual<uint8_t>(At<void*>(node, 0x8), 0x0, 0xA821EF94u, &network) &&
                Virtual<uint8_t>(network, 0x24, 0x449u)) {
                const int32_t own = 0;
                *AccessAt(self, &own) = 1;
                Virtual<void>(occupants, 0xC0, &list);
                if (network)
                    Release(network);
                DeleteList(&list);
                return 1;
            }
            if (network)
                Release(network);
        }
        Virtual<void>(occupants, 0xC0, &list);
        DeleteList(&list);
    }
    const int32_t own = 0;
    *AccessAt(self, &own) = 0;
    return 0;
}

// 0x006C17E0: road access for a purpose, stored in the access map. True with
// the lot's own access. Otherwise the entry is set false first (which ends
// the recursion), then the lot's neighbours on the four sides (the lot
// manager's slot 9 with 1, 2, 4, 8; a list of referenced lots in STL nodes)
// are asked in turn until one is true: a neighbour in state 2-4 whose
// building profile has this purpose answers with its slot 29.
uint8_t NeighbourRoadAccess(char* self, int32_t purpose)
{
    OPENSC4_CITYLOT_FIRST("NeighbourRoadAccess");
    if (Virtual<uint8_t>(self, slot::kHasRoadAccess)) {
        *AccessAt(self, &purpose) = 1;
        return 1;
    }
    *AccessAt(self, &purpose) = 0;
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    void* const development = Global<void*>(addr::SC4_Service43D68);
    uint8_t access = 0;
    char* list = static_cast<char*>(Allocate(0xC));
    At<char*>(list, 0x0) = list;
    At<char*>(list, 0x4) = list;
    Virtual<void>(lotManager, 0x24, self, &list, 1u);
    Virtual<void>(lotManager, 0x24, self, &list, 2u);
    Virtual<void>(lotManager, 0x24, self, &list, 4u);
    Virtual<void>(lotManager, 0x24, self, &list, 8u);
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const neighbour = At<void*>(node, 0x8);
        if (access)
            continue;
        const int32_t state = Virtual<int32_t>(neighbour, slot::kGetState);
        if (state < 2 || state > 4)
            continue;
        char* const profile =
            Virtual<char*>(development, 0x9C, Virtual<uint32_t>(neighbour, slot::kGetBuildingType, 1u));
        if (At<int32_t>(profile, 0x0) != purpose)
            continue;
        access = Virtual<uint8_t>(neighbour, slot::kHasOrganicRoadAccess, purpose);
    }
    *AccessAt(self, &purpose) = access;
    for (char* node = At<char*>(list, 0x0); node != list;) {
        char* const next = At<char*>(node, 0x0);
        if (void* const lot = At<void*>(node, 0x8))
            Release(lot);
        Free(node);
        node = next;
    }
    At<char*>(list, 0x0) = list;
    At<char*>(list, 0x4) = list;
    Free(list);
    return access;
}

// Slot 29 (0x006C1D10): road access for a purpose. The lot's own access
// (the map's entry 0, computed when missing) is enough; else, for a non-zero
// purpose, the purpose's entry, computed through the neighbours when missing.
uint8_t __fastcall HasOrganicRoadAccess(char* self, void*, int32_t purpose)
{
    OPENSC4_CITYLOT_FIRST("HasOrganicRoadAccess");
    const auto find = reinterpret_cast<FindSignedFn>(addr::SC4Sint32Map_Find);
    int32_t own = 0;
    char* node = nullptr;
    find(self + field::kMap40, &node, &own);
    const uint8_t access =
        node != At<char*>(self, field::kMap40) ? At<uint8_t>(node, 0x14) : ComputeRoadAccess(self);
    if (access)
        return 1;
    if (purpose == 0)
        return 0;
    find(self + field::kMap40, &node, &purpose);
    if (node != At<char*>(self, field::kMap40))
        return At<uint8_t>(node, 0x14);
    return NeighbourRoadAccess(self, purpose);
}

// Slot 30 (0x006C1DA0): recomputes the lot's own access, then every other
// purpose in the access map.
void __fastcall UpdateRoadAccess(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("UpdateRoadAccess");
    ComputeRoadAccess(self);
    for (char* node = At<char*>(At<char*>(self, field::kMap40), 0x8); node != At<char*>(self, field::kMap40);
         node = TreeNext(node)) {
        if (const int32_t purpose = At<int32_t>(node, 0x10))
            NeighbourRoadAccess(self, purpose);
    }
}

// Slot 28 (0x006C20E0): slot 29 for purpose 0.
uint8_t __fastcall HasRoadAccess(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("HasRoadAccess");
    return Virtual<uint8_t>(self, slot::kHasOrganicRoadAccess, 0);
}

// --- Getters and setters -------------------------------------------------------------

// Slot 3 (0x006C2040): the property holder part, null for a null lot.
char* __fastcall AsPropertyHolder(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("AsPropertyHolder");
    return self ? self + field::kHolder : nullptr;
}

// Slot 4 (0x006C2050): the traffic source part.
char* __fastcall AsTrafficSource(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("AsTrafficSource");
    return self ? self + field::kTrafficSource : nullptr;
}

// Slot 6 (0x006C2020), AL.
uint8_t __fastcall IsInitialized(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("IsInitialized");
    return At<uint8_t>(self, field::kInitialized);
}

// Slot 12 (0x006C2030).
uint32_t __fastcall GetFlags(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetFlags");
    return At<uint8_t>(self, field::kFlags);
}

// Slots 16, 18, 19 (0x006C2060, 0x006C2070, 0x006C2080): floats in ST0.
float __fastcall GetAltitude(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetAltitude");
    return At<float>(self, field::kAltitude);
}

float __fastcall GetSlope(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetSlope");
    return At<float>(self, field::kSlope);
}

float __fastcall GetSlopeAtTimeOfConstruction(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetSlopeAtTimeOfConstruction");
    return At<float>(self, field::kBuiltSlope);
}

// Slot 23 (0x006C2090).
uint32_t __fastcall GetFacing(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetFacing");
    return At<uint8_t>(self, field::kFacing);
}

// Slot 24 (0x006C20A0): the argument's low byte (which the game leaves in AL).
uint8_t __fastcall SetFacing(char* self, void*, uint32_t facing)
{
    OPENSC4_CITYLOT_FIRST("SetFacing");
    At<uint8_t>(self, field::kFacing) = static_cast<uint8_t>(facing);
    return static_cast<uint8_t>(facing);
}

// Slot 26 (0x006C20B0): the business unit; the old one released, the new one
// referenced, nothing when it is the same.
void __fastcall SetUnit(char* self, void*, void* unit)
{
    OPENSC4_CITYLOT_FIRST("SetUnit");
    void* const old = At<void*>(self, field::kUnit);
    if (unit == old)
        return;
    if (old)
        Release(old);
    At<void*>(self, field::kUnit) = unit;
    if (unit)
        AddRef(unit);
}

// Slots 31, 33, 35 (0x006C20F0, 0x006C2100, 0x006C2110).
uint32_t __fastcall GetZoneType(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetZoneType");
    return At<uint8_t>(self, field::kZoneType);
}

uint32_t __fastcall GetState(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetState");
    return At<uint8_t>(self, field::kState);
}

uint32_t __fastcall GetCondition(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetCondition");
    return At<uint8_t>(self, field::kCondition);
}

// Slot 37 (0x006C2150): state 3 (EAX 0 or 1).
uint32_t __fastcall IsInLivableState(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("IsInLivableState");
    return At<uint8_t>(self, field::kState) == 3 ? 1 : 0;
}

// Slot 38 (0x006C2160): state 2 or 3.
uint32_t __fastcall HasCapacity(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("HasCapacity");
    const uint8_t state = At<uint8_t>(self, field::kState);
    return state >= 2 && state < 4 ? 1 : 0;
}

// Slot 39 (0x006C2180): state 2-4 or 6.
uint32_t __fastcall IsInDemolishableState(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("IsInDemolishableState");
    const uint8_t state = At<uint8_t>(self, field::kState);
    return (state >= 2 && state <= 4) || state == 6 ? 1 : 0;
}

// Slot 41 (0x006C21F0).
uint32_t __fastcall GetOccupantWealth(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetOccupantWealth");
    return At<uint8_t>(self, field::kWealth);
}

// A flag of +0x79 (AL), and its setter: the game's leave the new flags byte
// in AL.
uint8_t Flag(char* self, uint8_t mask) { return (At<uint8_t>(self, field::kFlags) & mask) ? 1 : 0; }

uint8_t SetFlag(char* self, uint8_t mask, uint32_t on)
{
    uint8_t& flags = At<uint8_t>(self, field::kFlags);
    if (static_cast<uint8_t>(on))
        flags = static_cast<uint8_t>(flags | mask);
    else
        flags = static_cast<uint8_t>(flags & ~mask);
    return flags;
}

// Slots 43, 44 (0x006C2220, 0x006C2230): flag 0x20.
uint8_t __fastcall IsHistorical(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("IsHistorical");
    return Flag(self, 0x20);
}

uint8_t __fastcall SetHistorical(char* self, void*, uint32_t historical)
{
    OPENSC4_CITYLOT_FIRST("SetHistorical");
    return SetFlag(self, 0x20, historical);
}

// Slot 45 (0x006C2250).
uint32_t __fastcall GetVacateReason(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetVacateReason");
    return At<uint8_t>(self, field::kVacateReason);
}

// Slot 47 (0x006C2200): the building type; the occupant wealth back to 0.
uint32_t __fastcall SetBuildingType(char* self, void*, uint32_t buildingType)
{
    OPENSC4_CITYLOT_FIRST("SetBuildingType");
    At<uint32_t>(self, field::kBuildingType) = buildingType;
    At<uint8_t>(self, field::kWealth) = 0;
    return buildingType;
}

// Slot 54 (0x006C21E0): the populations vector's address.
char* __fastcall GetPopulations(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetPopulations");
    return self + field::kPopulations;
}

// Slots 65, 66, 67 (0x006C21A0, 0x006C21B0, 0x006C21C0): flags 0x08, 0x10.
uint8_t __fastcall GetNeedsPower(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetNeedsPower");
    return Flag(self, 0x08);
}

uint8_t __fastcall GetHasPower(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetHasPower");
    return Flag(self, 0x10);
}

uint8_t __fastcall SetHasPower(char* self, void*, uint32_t hasPower)
{
    OPENSC4_CITYLOT_FIRST("SetHasPower");
    return SetFlag(self, 0x10, hasPower);
}

// Slots 68, 69 (0x006C2120, 0x006C2130): flag 0x40.
uint8_t __fastcall GetVerticesAreLocked(char* self, void*)
{
    OPENSC4_CITYLOT_FIRST("GetVerticesAreLocked");
    return Flag(self, 0x40);
}

uint8_t __fastcall SetVerticesAreLocked(char* self, void*, uint32_t locked)
{
    OPENSC4_CITYLOT_FIRST("SetVerticesAreLocked");
    return SetFlag(self, 0x40, locked);
}

// Slot 5 of the traffic source part (0x006C22D0; `part` = lot + 0x1C): the
// cell rectangle as four int32 (min x, min z, max x, max z); returns out.
int32_t* __fastcall SourceGetCellRect(char* part, void*, int32_t* out)
{
    OPENSC4_CITYLOT_FIRST("SourceGetCellRect");
    out[1] = At<uint8_t>(part, 0x5);
    out[2] = At<uint8_t>(part, 0x6);
    out[0] = At<uint8_t>(part, 0x4);
    out[3] = At<uint8_t>(part, 0x7);
    return out;
}

// --- The business unit ---------------------------------------------------------------

// Slot 4 of the unit's cIGZSerializable part (0x006C09E0; `part` = unit +
// 4): a uint16 version that must be 1 (else false), a uint32 count, then
// that many lot references, appended to the member list (referenced by the
// list). True (AL) when the stream has no error. The version is read into
// the argument's own stack slot, so a stream that writes nothing leaves the
// stream pointer's low word there. A reference that reads as null is released
// all the same, as in the game (which crashes there).
uint8_t __fastcall UnitRead(char* part, void*, void* stream)
{
    OPENSC4_CITYLOT_FIRST("UnitRead");
    uint32_t version = reinterpret_cast<uint32_t>(stream);
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 1)
        return 0;
    uint32_t count = 0; // the game's is not initialised
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* lot = nullptr;
        reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, kIidLot, &lot, 0);
        void* const held = lot;
        if (held)
            AddRef(held);
        char* const head = At<char*>(part, 0x18);
        char* const node = static_cast<char*>(Allocate(0xC));
        At<void*>(node, 0x8) = held;
        if (held)
            AddRef(held);
        char* const previous = At<char*>(head, 0x4);
        At<char*>(node, 0x0) = head;
        At<char*>(node, 0x4) = previous;
        At<char*>(previous, 0x0) = node;
        At<char*>(head, 0x4) = node;
        if (held)
            Release(held);
        Release(lot);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 24 of the unit's holder part (the thunk 0x006C0B90; `holder` = unit +
// 8): Read with unit + 4.
uint8_t __fastcall UnitReadFromHolder(char* holder, void*, void* stream)
{
    return UnitRead(holder - 4, nullptr, stream);
}

// 0x006C0EB0: the class factory: 0x20 bytes from the game's operator new,
// constructed by the object part's 0x006C0800; null without memory.
void* __cdecl UnitCreate()
{
    OPENSC4_CITYLOT_FIRST("UnitCreate");
    void* const memory = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0x20);
    if (!memory)
        return nullptr;
    return reinterpret_cast<ConstructFn>(addr::cSC4BusinessUnit_cSC4BusinessUnit)(memory);
}

// 0x006C0ED0: the unit's deleting destructor: the three vtables set again,
// the member list emptied (each lot released) and its sentinel freed, the
// holder base destroyed, the memory given back when flags & 1.
char* __fastcall UnitDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_CITYLOT_FIRST("UnitDeletingDestructor");
    At<std::uintptr_t>(self, 0x0) = addr::cSC4BusinessUnit_vftable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4BusinessUnit_vftable_4;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4BusinessUnit_vftable_8;
    reinterpret_cast<ThisFn>(addr::SC4RefList_Clear)(self + 0x1C);
    if (void* const head = At<void*>(self, 0x1C))
        Free(head);
    reinterpret_cast<ThisFn>(addr::SC4PropertyHolder_Destroy)(self + 0x8);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return self;
}

// --- The trip list -------------------------------------------------------------------

// 0x006C0AE0: a trip record's copy constructor; returns the record.
char* __fastcall TripRecordCopy(char* self, void*, const char* source)
{
    OPENSC4_CITYLOT_FIRST("TripRecordCopy");
    const auto copyVector = reinterpret_cast<CopyVectorFn>(addr::SC4ByteVector_CopyConstruct);
    copyVector(self, source);
    copyVector(self + 0xC, source + 0xC);
    At<uint32_t>(self, 0x18) = At<uint32_t>(const_cast<char*>(source), 0x18);
    At<uint8_t>(self, 0x1C) = At<uint8_t>(const_cast<char*>(source), 0x1C);
    At<uint8_t>(self, 0x1D) = At<uint8_t>(const_cast<char*>(source), 0x1D);
    At<uint8_t>(self, 0x1E) = At<uint8_t>(const_cast<char*>(source), 0x1E);
    At<uint8_t>(self, 0x1F) = At<uint8_t>(const_cast<char*>(source), 0x1F);
    At<uint16_t>(self, 0x20) = At<uint16_t>(const_cast<char*>(source), 0x20);
    At<uint16_t>(self, 0x22) = At<uint16_t>(const_cast<char*>(source), 0x22);
    At<uint32_t>(self, 0x24) = At<uint32_t>(const_cast<char*>(source), 0x24);
    At<uint32_t>(self, 0x28) = At<uint32_t>(const_cast<char*>(source), 0x28);
    return self;
}

// 0x006C1E10: a copy of a record in a new 0x34-byte node at the front of a
// trip list. EAX is left as the game leaves it: the node that was before the
// old first one (the sentinel).
char* __fastcall TripListPushFront(char** list, void*, const char* record)
{
    OPENSC4_CITYLOT_FIRST("TripListPushFront");
    char* const first = At<char*>(*list, 0x0);
    char* const node = static_cast<char*>(Allocate(0x34));
    TripRecordCopy(node + 0x8, nullptr, record);
    char* const previous = At<char*>(first, 0x4);
    At<char*>(node, 0x0) = first;
    At<char*>(node, 0x4) = previous;
    At<char*>(previous, 0x0) = node;
    At<char*>(first, 0x4) = node;
    return previous;
}

// 0x006C0FA0: unlinks and frees a trip list's node (its two byte vectors,
// the second first); *out gets the next node. Returns out.
char** __fastcall TripListErase(void*, void*, char** out, char* node)
{
    OPENSC4_CITYLOT_FIRST("TripListErase");
    char* const previous = At<char*>(node, 0x4);
    char* const next = At<char*>(node, 0x0);
    At<char*>(previous, 0x0) = next;
    At<char*>(next, 0x4) = previous;
    if (void* const second = At<void*>(node, 0x14))
        Free(second);
    if (void* const first = At<void*>(node, 0x8))
        Free(first);
    Free(node);
    *out = next;
    return out;
}

// --- Installation --------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}
#define OPENSC4_LOT_SLOT(index, original, fn) OPENSC4_SLOT(addr::cSC4Lot_vftable, index, original, fn)

const Slot kSlots[] = {
    OPENSC4_LOT_SLOT(3, 0x006C2040, AsPropertyHolder),
    OPENSC4_LOT_SLOT(4, 0x006C2050, AsTrafficSource),
    OPENSC4_LOT_SLOT(6, 0x006C2020, IsInitialized),
    OPENSC4_LOT_SLOT(12, 0x006C2030, GetFlags),
    OPENSC4_LOT_SLOT(16, 0x006C2060, GetAltitude),
    OPENSC4_LOT_SLOT(18, 0x006C2070, GetSlope),
    OPENSC4_LOT_SLOT(19, 0x006C2080, GetSlopeAtTimeOfConstruction),
    OPENSC4_LOT_SLOT(23, 0x006C2090, GetFacing),
    OPENSC4_LOT_SLOT(24, 0x006C20A0, SetFacing),
    OPENSC4_LOT_SLOT(26, 0x006C20B0, SetUnit),
    OPENSC4_LOT_SLOT(28, 0x006C20E0, HasRoadAccess),
    OPENSC4_LOT_SLOT(29, 0x006C1D10, HasOrganicRoadAccess),
    OPENSC4_LOT_SLOT(30, 0x006C1DA0, UpdateRoadAccess),
    OPENSC4_LOT_SLOT(31, 0x006C20F0, GetZoneType),
    OPENSC4_LOT_SLOT(33, 0x006C2100, GetState),
    OPENSC4_LOT_SLOT(35, 0x006C2110, GetCondition),
    OPENSC4_LOT_SLOT(37, 0x006C2150, IsInLivableState),
    OPENSC4_LOT_SLOT(38, 0x006C2160, HasCapacity),
    OPENSC4_LOT_SLOT(39, 0x006C2180, IsInDemolishableState),
    OPENSC4_LOT_SLOT(41, 0x006C21F0, GetOccupantWealth),
    OPENSC4_LOT_SLOT(43, 0x006C2220, IsHistorical),
    OPENSC4_LOT_SLOT(44, 0x006C2230, SetHistorical),
    OPENSC4_LOT_SLOT(45, 0x006C2250, GetVacateReason),
    OPENSC4_LOT_SLOT(47, 0x006C2200, SetBuildingType),
    OPENSC4_LOT_SLOT(54, 0x006C21E0, GetPopulations),
    OPENSC4_LOT_SLOT(55, 0x006C1940, SetPopulation),
    OPENSC4_LOT_SLOT(58, 0x006C0BA0, AddTravelPopulation),
    OPENSC4_LOT_SLOT(61, 0x006C0CF0, GetOccupancy),
    OPENSC4_LOT_SLOT(62, 0x006C10F0, GetOccupancyByType),
    OPENSC4_LOT_SLOT(65, 0x006C21A0, GetNeedsPower),
    OPENSC4_LOT_SLOT(66, 0x006C21B0, GetHasPower),
    OPENSC4_LOT_SLOT(67, 0x006C21C0, SetHasPower),
    OPENSC4_LOT_SLOT(68, 0x006C2120, GetVerticesAreLocked),
    OPENSC4_LOT_SLOT(69, 0x006C2130, SetVerticesAreLocked),
    OPENSC4_SLOT(addr::cSC4Lot_vftable_1C, 5, 0x006C22D0, SourceGetCellRect),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable_4, 4, 0x006C09E0, UnitRead),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable_8, 24, 0x006C0B90, UnitReadFromHolder),
};

#undef OPENSC4_LOT_SLOT
#undef OPENSC4_SLOT

using WealthFn = void(__fastcall*)(char*, void*);
using RecordCopyFn = char*(__fastcall*)(char*, void*, const char*);
using PushFrontFn = char*(__fastcall*)(char**, void*, const char*);
using EraseFn = char**(__fastcall*)(void*, void*, char**, char*);
using CreateFn = void*(__cdecl*)();
using DestructorFn = char*(__fastcall*)(char*, void*, uint32_t);

WealthFn g_originalWealth = nullptr;
RecordCopyFn g_originalRecordCopy = nullptr;
PushFrontFn g_originalPushFront = nullptr;
EraseFn g_originalErase = nullptr;
CreateFn g_originalCreate = nullptr;
DestructorFn g_originalDestructor = nullptr;

template <typename Fn>
bool Detour(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* const entry = addr_check::Find(address);
    if (!entry) {
        log::Write("citylot capacity: no address entry for %08X; not installing", static_cast<unsigned>(address));
        return false;
    }
    return hook::Install(*entry, ours, original);
}

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const std::uintptr_t held = reinterpret_cast<std::uintptr_t*>(slot.vtable)[slot.index];
        if (held != slot.original) {
            log::Write("citylot capacity: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(held),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    if (!Detour(addr::cSC4Lot_Fn_006C1430, &UpdateOccupantWealth, &g_originalWealth) ||
        !Detour(addr::cSC4Lot_Fn_006C0AE0, &TripRecordCopy, &g_originalRecordCopy) ||
        !Detour(addr::cSC4Lot_Fn_006C1E10, &TripListPushFront, &g_originalPushFront) ||
        !Detour(addr::cSC4LotTripList_Erase, &TripListErase, &g_originalErase) ||
        !Detour(addr::cSC4BusinessUnit_Create, &UnitCreate, &g_originalCreate) ||
        !Detour(addr::cSC4BusinessUnit_DeletingDestructor, &UnitDeletingDestructor, &g_originalDestructor))
        return false;
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("citylot capacity: %u slots and 6 detours are ours", static_cast<unsigned>(std::size(kSlots)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.citylot.capacity", "simulators/citylot", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4Lot_vftable,
     "Our cSC4Lot capacity part: population, travel population, occupancy, occupant wealth, road access, the small "
     "getters and setters; the business unit's Read, factory and destructor; the trip list's node helpers",
     false, Install});

} // namespace
} // namespace opensc4::citylot_sim
