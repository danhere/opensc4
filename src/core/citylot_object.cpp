// Our cSC4Lot (build 641; src/core/citylot_sim.h has the class map), the
// object part: our own object (the three constructors, the destructor,
// identity), Init, Shutdown, PostCityInit, Write (the save record; Read is
// in citylot_state.cpp) and the functions with their entry in
// 0x006BF180-0x006C09E0, cSC4BusinessUnit's among them.
//
// Fields this file uses (offsets from the lot's start; evidence in
// citylot_sim.h's field list; all of them appear in the constructors
// 0x006C1E50 / 0x006C2340 / 0x006C2450 and in Write 0x006C0080, which reads
// 4 less because it gets lot + 4):
//   +0x08 the property holder part; +0x0C uint32 reference count
//   +0x20-+0x23 uint8 cell rectangle; +0x24, +0x28, +0x2C float altitude,
//   slope, slope when built; +0x30 uint32 configuration ID from a save;
//   +0x34 the lot configuration (referenced); +0x38 uint32 day of the last
//   state change; +0x3C uint32 building type; +0x40 a tree (head, size);
//   +0x4C the business unit; +0x50 the travel designate (referenced);
//   +0x54 vector of 12-byte maps uint32 -> uint16 (populations); +0x60 map
//   uint32 -> uint16 (travel population; size +0x64); +0x6C float[3] travel
//   jobs; +0x78 uint8 initialized; +0x79 uint8 flags; +0x7A, +0x7B cell;
//   +0x7C, +0x7D size; +0x7E facing; +0x7F state; +0x80 condition; +0x81
//   zone type; +0x82 wealth; +0x83 vacate reason; +0x84 trip list head;
//   +0x88 uint8 random 0-99 (0xFF unset)
// cSC4BusinessUnit: +0x0C uint32 reference count, +0x1C list head of
// member lots (nodes: next, previous, lot; one reference each).
//
// Argument counts of every virtual call here were taken from the callee
// (the slot's ret N in the receiver's vtable): the lot 0x00AB19B0, the
// building development simulator 0x00AB0AF8 (slots 30, 33, 36, 39), the lot
// developer 0x00AB1B98 (30, 39), the traffic simulator 0x00AB33D0 (19, 20,
// 38), the demand simulator 0x00AB0E48 (8), the occupant manager
// 0x00A8F7F0 (35, 40), the zone manager 0x00A92E00 (12), the lot
// configuration manager 0x00A8EC58 (8), the simulator 0x00AB2FB8 (7) and
// its date 0x00ACEA38 (26), the terrain 0x00AB3D18 (13) and its picker
// 0x00AB4410 (16, 17), the land value simulator 0x00AB1670 (6: a tail jump
// to its grid's slot 7, 2 arguments), the lot configuration 0x00A8CEB8 (3,
// 11, 22, 23), the building occupant 0x00A872C0 (3), occupants (6, 8), the
// property holder (9), properties (5), variants 0x00AA6788 (4, 72, 103,
// 111), the growth developer 0x00AB1488 (19) and the save streams
// 0x00AA2310 (5, 7, 9, 12, 18).
#include "core/citylot_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::citylot_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("citylot: first %s", name);
}

namespace {

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidBuildingOccupant = 0x87DDA3A9;

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

void* Allocate(std::size_t size) { return CallCdecl<void*, std::size_t>(addr::SC4_stl_allocate, size); }
void Free(void* block) { CallCdecl<void, void*>(addr::SC4_Free, block); }

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The game's float to int: the value plus 1.5 * 2^23 (0x00AB16B4) stored as
// a float, whose bits less 0x4B400000 are the value rounded to nearest.
int32_t RoundViaFloat(double value)
{
    const float sum = static_cast<float>(value + 12582912.0);
    return static_cast<int32_t>(Bits(sum) + 0xB4C00000u);
}

void* Service(std::uintptr_t global) { return Global<void*>(global); }

// --- Trees ----------------------------------------------------------------------

// A new empty tree (head of 0x18 bytes: color 0, no root, first and last
// the head itself), as the constructors build +0x40 and +0x60.
void NewTree(char* tree)
{
    At<char*>(tree, 0x0) = nullptr;
    char* const head = static_cast<char*>(Allocate(0x18));
    At<char*>(tree, 0x0) = head;
    At<uint32_t>(tree, 0x4) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
}

void ResetHead(char* tree)
{
    char* const head = At<char*>(tree, 0x0);
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(tree, 0x4) = 0;
}

// 0x006BFE60: the nodes of the tree +0x40 freed (right subtrees first, down
// the left spine); their values own nothing.
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        Free(node);
        node = left;
    }
}

// The inline clear of the travel population map +0x60: the nodes freed by
// the game's 0x00458E00 (generic, shared), the head reset.
void ClearTravel(char* self)
{
    char* const tree = self + field::kTravel;
    if (!At<uint32_t>(tree, 0x4))
        return;
    CallThis<void, void*>(addr::SC4Uint32Uint16Map_EraseNodes, tree, At<void*>(At<char*>(tree, 0x0), 0x4));
    ResetHead(tree);
}

// The game's std::map<uint32, T>::find (0x00497520, generic): the node, or
// the head for none.
char* Find(char* map, uint32_t key)
{
    char* node = nullptr;
    CallThis<void, char**, const uint32_t*>(addr::SC4Uint32Map_Find, map, &node, &key);
    return node;
}

void AddRefOf(void* object) { Virtual<uint32_t>(object, 0x4); }
void ReleaseOf(void* object) { Virtual<uint32_t>(object, 0x8); }

// --- Slots in 0x006BF180-0x006C09E0 -------------------------------------------

// 0x006BF6A0 (not a slot; ResetTravelPopulation and AddTravelPopulation
// end in it): the three travel job counts, rounded, sent to the traffic
// simulator as the trip capacities 0, 1, 2 of the lot's holder part (slot
// 3) when they are more than a tenth off. The game always asks the current
// value of capacity 0: a count is skipped when it is within a tenth of the
// $ jobs, so high-tech industry (10/80/10) never offers its $$$ jobs. Kept
// as the game's unless fixes.travel_jobs is on, which asks capacity i.
bool g_fixTravelJobs = false;

void __fastcall PushTravelJobs(char* self, void*)
{
    static bool logged = false;
    FirstCall("PushTravelJobs", logged);
    void* const traffic = Service(addr::SC4_Service43DA8);
    if (!traffic)
        return;
    void* const source = Virtual<void*>(self, slot::kAsPropertyHolder);
    for (uint32_t i = 0; i < 3; ++i) {
        const int32_t jobs = RoundViaFloat(static_cast<double>(At<float>(self, field::kTravelJobs + 4 * i)));
        const int32_t current = Virtual<int32_t>(traffic, 0x50, source, g_fixTravelJobs ? i : 0u);
        if (jobs == current)
            continue;
        const int32_t larger = jobs < current ? current : jobs;
        const uint32_t distance =
            jobs - current < 0 ? 0u - static_cast<uint32_t>(jobs - current) : static_cast<uint32_t>(jobs - current);
        const double ratio = static_cast<double>(static_cast<int32_t>(distance)) / static_cast<double>(larger);
        if (ratio > static_cast<double>(0.1f))
            Virtual<void>(traffic, 0x4C, source, i, jobs);
    }
}

// 0x006BFD20 (not a slot; SetState calls it): the needs-power flag 0x08.
// Clear unless the state is 3 or 4 and the lot has a building type. Then:
// when the type's profile names a developer and the lot has a
// configuration, clear if the configuration's slot 11 value is not above
// the developer's slot 19; set at once without a building; with one, set
// when its property 0x27812854 is non-zero and 0x27812852 is not above 0.
void __fastcall UpdateNeedsPower(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateNeedsPower", logged);
    const auto Clear = [&] { At<uint8_t>(self, field::kFlags) &= 0xF7; };
    const int32_t state = Virtual<int32_t>(self, slot::kGetState);
    if (state < 3 || state > 4)
        return Clear();
    const uint32_t type = Virtual<uint32_t>(self, slot::kGetBuildingType, 0u);
    if (!type)
        return Clear();
    void* const development = Service(addr::SC4_Service43D68);
    void* const building = Virtual<void*>(self, slot::kGetBuilding);
    uint32_t* const profile = Virtual<uint32_t*>(development, 0x9C, type);
    if (profile[0]) {
        void* const developer = Virtual<void*>(development, 0x84, profile[0], profile[1]);
        void* const config = Virtual<void*>(self, slot::kGetLotConfiguration);
        if (!developer || !config)
            return Clear();
        const uint8_t cap = Virtual<uint8_t>(developer, 0x4C);
        if (Virtual<uint8_t>(config, 0x2C) <= cap)
            return Clear();
    }
    bool none = true;
    if (building) {
        void* const holder = Virtual<void*>(Virtual<void*>(building, 0xC), 0x18);
        if (void* const property = Virtual<void*>(holder, 0x24, 0x27812852u)) {
            if (Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x120) > 0)
                none = false;
        }
        void* const property = Virtual<void*>(holder, 0x24, 0x27812854u);
        if (!property)
            return Clear();
        if (!Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x120) || !none)
            return Clear();
    }
    At<uint8_t>(self, field::kFlags) |= 0x08;
}

// Slot 13 (0x006BF220): the flags stored; when bit 0 changed, every cell of
// the rectangle is marked in the terrain picker (slot 16 with the lot's
// altitude) or unmarked (slot 17).
void __fastcall SetFlags(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetFlags", logged);
    const uint8_t old = At<uint8_t>(self, field::kFlags);
    At<uint8_t>(self, field::kFlags) = static_cast<uint8_t>(flags);
    if (!((flags ^ old) & 1))
        return;
    void* const picker = CallCdecl<void*>(addr::SC4_TerrainPicker);
    if (!picker)
        return;
    const bool mark = At<uint8_t>(self, field::kFlags) & 1;
    for (int32_t x = At<uint8_t>(self, field::kMinX); x <= At<uint8_t>(self, field::kMaxX); ++x)
        for (int32_t z = At<uint8_t>(self, field::kMinZ); z <= At<uint8_t>(self, field::kMaxZ); ++z) {
            if (mark)
                Virtual<void>(picker, 0x40, x, z, At<uint32_t>(self, field::kAltitude));
            else
                Virtual<void>(picker, 0x44, x, z);
        }
}

// Slot 14 (0x006BF320).
void __fastcall GetBoundingRect(char* self, void*, int32_t* rect)
{
    static bool logged = false;
    FirstCall("GetBoundingRect", logged);
    rect[0] = At<uint8_t>(self, field::kMinX);
    rect[1] = At<uint8_t>(self, field::kMinZ);
    rect[2] = At<uint8_t>(self, field::kMaxX);
    rect[3] = At<uint8_t>(self, field::kMaxZ);
}

// Slot 15 (0x006BF560): the middle of the rectangle in metres (16 per
// cell) with the lot's altitude between. EAX is the out pointer.
float* __fastcall GetCenter(char* self, void*, float* center)
{
    static bool logged = false;
    FirstCall("GetCenter", logged);
    const int32_t x = (At<uint8_t>(self, field::kMinX) << 4) + ((At<uint8_t>(self, field::kMaxX) + 1) << 4);
    const int32_t z = (At<uint8_t>(self, field::kMinZ) << 4) + ((At<uint8_t>(self, field::kMaxZ) + 1) << 4);
    center[0] = static_cast<float>(static_cast<double>(x) * 0.5);
    At<uint32_t>(center, 0x4) = At<uint32_t>(self, field::kAltitude);
    center[2] = static_cast<float>(static_cast<double>(z) * 0.5);
    return center;
}

// The lot developer's slope of the lot (the building development
// simulator's slot 36 object, slot 30 with the lot and 0).
float LotSlope(char* self)
{
    void* const developer = Virtual<void*>(Service(addr::SC4_Service43D68), 0x90);
    return static_cast<float>(Virtual<double>(developer, 0x78, static_cast<void*>(self), 0u));
}

// Slot 17 (0x006BF5C0): when the altitude moves by more than 0.001, it is
// stored and both slopes are taken anew.
void __fastcall SetAltitude(char* self, void*, float altitude)
{
    static bool logged = false;
    FirstCall("SetAltitude", logged);
    const double distance =
        std::fabs(static_cast<double>(altitude) - static_cast<double>(At<float>(self, field::kAltitude)));
    if (!(distance > 0.001))
        return;
    At<uint32_t>(self, field::kAltitude) = Bits(altitude);
    const float slope = LotSlope(self);
    At<float>(self, field::kBuiltSlope) = slope;
    At<float>(self, field::kSlope) = slope;
}

// Slot 20 (0x006BF610).
void __fastcall UpdateSlope(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateSlope", logged);
    At<float>(self, field::kSlope) = LotSlope(self);
}

// Slot 21 (0x006BF2E0).
void __fastcall GetLocation(char* self, void*, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("GetLocation", logged);
    *x = At<uint8_t>(self, field::kCellX);
    *z = At<uint8_t>(self, field::kCellZ);
}

// Slot 22 (0x006BF300).
void __fastcall GetSize(char* self, void*, uint8_t* width, uint8_t* depth)
{
    static bool logged = false;
    FirstCall("GetSize", logged);
    *width = At<uint8_t>(self, field::kWidth);
    *depth = At<uint8_t>(self, field::kDepth);
}

// Slot 27 (0x006BFCE0): the traffic simulator's slot 38 above 0 for kind 0
// or 1.
uint8_t __fastcall HasJobAccess(char* self, void*)
{
    static bool logged = false;
    FirstCall("HasJobAccess", logged);
    void* const traffic = Service(addr::SC4_Service43DA8);
    for (uint32_t kind = 0; kind < 2; ++kind)
        if (Virtual<uint32_t>(traffic, 0x98, static_cast<void*>(self), kind) > 0)
            return 1;
    return 0;
}

// Slot 32 (0x006BFC40): the zone manager's type at the lot's cell.
void __fastcall UpdateZoneType(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateZoneType", logged);
    At<uint8_t>(self, field::kZoneType) =
        Virtual<uint8_t>(Service(addr::SC4_ZoneManager), 0x30, uint32_t{At<uint8_t>(self, field::kCellX)},
                         uint32_t{At<uint8_t>(self, field::kCellZ)});
}

// Slot 40 (0x006BF630): today's day number less the day stored; 0 when
// none is.
uint32_t __fastcall GetDaysSinceLastStateChange(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetDaysSinceLastStateChange", logged);
    if (!At<uint32_t>(self, field::kStateDay))
        return 0;
    void* const date = Virtual<void*>(Service(addr::SC4_SimulatorService), 0x1C);
    return Virtual<uint32_t>(date, 0x68) - At<uint32_t>(self, field::kStateDay);
}

// Slot 42 (0x006BFC60): the land value simulator's byte (slot 6, called
// with the z cell first) summed over the rectangle, in EDX:EAX.
int64_t __fastcall GetLandValue(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetLandValue", logged);
    void* const values = Service(addr::SC4_Service43D88);
    int64_t sum = 0;
    for (int32_t z = At<uint8_t>(self, field::kMinZ); z <= At<uint8_t>(self, field::kMaxZ); ++z)
        for (int32_t x = At<uint8_t>(self, field::kMinX); x <= At<uint8_t>(self, field::kMaxX); ++x)
            sum += Virtual<uint8_t>(values, 0x18, z, x);
    return sum;
}

// Slot 46 (0x006BFA00): the stored type; when asked to work it out and
// none is stored, the configuration's (its slot 3) unless the state is 6.
uint32_t __fastcall GetBuildingType(char* self, void*, uint8_t calculate)
{
    static bool logged = false;
    FirstCall("GetBuildingType", logged);
    if (calculate && !At<uint32_t>(self, field::kBuildingType)) {
        void* const config = At<void*>(self, field::kConfig);
        if (config && At<uint8_t>(self, field::kState) != 6)
            return Virtual<uint32_t>(config, 0xC);
    }
    return At<uint32_t>(self, field::kBuildingType);
}

// Slot 48 (0x006BFA30): with a building type, the first occupant of type
// 0x278128A0 in the rectangle (occupant manager slot 35) as a building
// occupant; not referenced for the caller.
void* __fastcall GetBuilding(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetBuilding", logged);
    if (!At<uint32_t>(self, field::kBuildingType))
        return nullptr;
    void* building = nullptr;
    void* occupant = nullptr;
    int32_t high[2] = {At<uint8_t>(self, field::kMaxX), At<uint8_t>(self, field::kMaxZ)};
    int32_t low[2] = {At<uint8_t>(self, field::kMinX), At<uint8_t>(self, field::kMinZ)};
    if (Virtual<uint8_t>(Service(addr::SC4_NetworkService), 0x8C, &occupant, static_cast<int32_t*>(low),
                         static_cast<int32_t*>(high), 0x278128A0u)) {
        if (Virtual<uint8_t>(occupant, 0x0, kIidBuildingOccupant, &building))
            ReleaseOf(building);
        ReleaseOf(occupant);
    }
    return building;
}

// Slot 49 (0x006BFF70): the occupants in the rectangle that the game's lot
// filter (0x38 bytes, constructor 0x006BFB20, set up for the lot by
// 0x006BFAC0; a small class of its own, left the game's) accepts, into the
// list (occupant manager slot 40).
void __fastcall GetLotOccupants(char* self, void*, void* list)
{
    static bool logged = false;
    FirstCall("GetLotOccupants", logged);
    int32_t high[2] = {At<uint8_t>(self, field::kMaxX), At<uint8_t>(self, field::kMaxZ)};
    int32_t low[2] = {At<uint8_t>(self, field::kMinX), At<uint8_t>(self, field::kMinZ)};
    void* filter = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, 0x38);
    if (filter)
        filter = CallThis<void*>(addr::cSC4LotOccupantFilter_Construct, filter);
    AddRefOf(filter);
    CallThis<void, void*>(addr::cSC4LotOccupantFilter_SetLot, filter, self);
    Virtual<void>(Service(addr::SC4_NetworkService), 0xA0, list, static_cast<int32_t*>(low),
                  static_cast<int32_t*>(high), filter, 0xFFFFFFFFu);
    ReleaseOf(filter);
}

// Slot 11 (0x006BFEA0): the object of the lot's configuration (0x34 bytes
// each; configuration slots 22 count, 23 the vector) whose place on the lot
// (lot developer slot 39) is within 0.1 of the occupant's in x and z.
void* __fastcall GetLotConfigurationObject(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationObject", logged);
    void* const config = At<void*>(self, field::kConfig);
    if (!config)
        return nullptr;
    const uint32_t count = Virtual<uint32_t>(config, 0x58);
    char** const objects = Virtual<char**>(At<void*>(self, field::kConfig), 0x5C);
    if (!objects)
        return nullptr;
    // The game's locals in its stack order: the fifth argument's dword, then
    // the place, then the occupant's position.
    struct {
        uint32_t other;
        float place[3];
        float position[3];
    } local = {};
    Virtual<void>(occupant, 0x20, static_cast<float*>(local.position));
    for (uint32_t i = 0; i < count; ++i) {
        char* const object = *objects + i * 0x34;
        Virtual<void>(Service(addr::SC4_Service43D20), 0x9C, static_cast<void*>(self), At<void*>(self, field::kConfig),
                      static_cast<void*>(object), static_cast<float*>(local.place), &local.other);
        const double limit = static_cast<double>(0.1f);
        if (std::fabs(static_cast<double>(local.place[0]) - static_cast<double>(local.position[0])) < limit &&
            std::fabs(static_cast<double>(local.place[2]) - static_cast<double>(local.position[2])) < limit)
            return object;
    }
    return nullptr;
}

// Slot 53 (0x006C04A0): the developer type's count summed over the maps of
// +0x54.
uint16_t __fastcall GetPopulation(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetPopulation", logged);
    uint16_t sum = 0;
    for (char* map = At<char*>(self, field::kPopulations); map != At<char*>(self, field::kPopulations + 4); map += 12) {
        char* const head = At<char*>(map, 0x0);
        char* found = head;
        for (char* node = At<char*>(head, 0x4); node;) {
            if (At<uint32_t>(node, 0x10) < type) {
                node = At<char*>(node, 0xC);
            } else {
                found = node;
                node = At<char*>(node, 0x8);
            }
        }
        if (found != head && type >= At<uint32_t>(found, 0x10))
            sum = static_cast<uint16_t>(sum + At<uint16_t>(found, 0x14));
    }
    return sum;
}

// Slot 56 (0x006C0510).
uint16_t __fastcall GetTravelPopulation(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetTravelPopulation", logged);
    char* const map = self + field::kTravel;
    char* const node = Find(map, type);
    return node != At<char*>(map, 0x0) ? At<uint16_t>(node, 0x14) : 0;
}

// Slot 57 (0x006C0550): the travel population and the travel jobs emptied,
// then the jobs sent on (a tail jump in the game).
void __fastcall ResetTravelPopulation(char* self, void*)
{
    static bool logged = false;
    FirstCall("ResetTravelPopulation", logged);
    ClearTravel(self);
    for (std::size_t i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kTravelJobs + 4 * i) = 0;
    CallThis<void>(addr::cSC4Lot_PushTravelJobs, self);
}

// Slot 50 (0x006C05A0): the capacity the building type's profile (maps at
// +0xC of it) gives the developer type; 0 when not asked to work it out
// and the state is not 2 or 3.
uint16_t __fastcall GetCapacityByType(char* self, void*, uint32_t type, uint8_t calculate)
{
    static bool logged = false;
    FirstCall("GetCapacityByType", logged);
    if (!calculate) {
        const uint8_t state = At<uint8_t>(self, field::kState);
        if (state < 2 || state >= 4)
            return 0;
    }
    const uint32_t building = Virtual<uint32_t>(self, slot::kGetBuildingType, 1u);
    if (!building)
        return 0;
    char* const profile = Virtual<char*>(Service(addr::SC4_Service43D68), 0x9C, building);
    char* map = At<char*>(profile, 0xC);
    const uint32_t count = static_cast<uint32_t>((At<char*>(profile, 0x10) - map) / 12);
    for (uint32_t i = 0; i < count; ++i, map += 12) {
        char* const node = Find(map, type);
        if (node != At<char*>(map, 0x0))
            return At<uint16_t>(node, 0x14);
    }
    return 0;
}

// Slot 51 (0x006BF9B0): the capacity of the developer type the profile's
// first value and the wealth give (development slot 30).
uint16_t __fastcall GetCapacityByWealth(char* self, void*, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetCapacityByWealth", logged);
    const uint32_t building = Virtual<uint32_t>(self, slot::kGetBuildingType, 1u);
    if (!building)
        return 0;
    void* const development = Service(addr::SC4_Service43D68);
    const uint32_t first = *Virtual<uint32_t*>(development, 0x9C, building);
    const uint32_t type = Virtual<uint32_t>(development, 0x78, first, wealth);
    return Virtual<uint16_t>(self, slot::kGetCapacityByType, type, 0u);
}

// Slot 52 (0x006C0640): in state 3 (or 4 too when asked to work it out),
// the developer type's capacity in the first profile map that has it; when
// other types of that map have populations in the lot's map of the same
// index, their shares (population / capacity) come off: without the
// profile's flag +0x18 nothing is left, with it the capacity times the
// rest, rounded. The lot's map is read at the profile map's index as the
// game does, whether or not the lot has that many.
uint16_t __fastcall GetAvailableCapacity(char* self, void*, uint32_t type, uint8_t calculate)
{
    static bool logged = false;
    FirstCall("GetAvailableCapacity", logged);
    const uint8_t state = At<uint8_t>(self, field::kState);
    if (calculate ? (state < 3 || state > 4) : state != 3)
        return 0;
    void* const development = Service(addr::SC4_Service43D68);
    char* const profile = Virtual<char*>(development, 0x9C, Virtual<uint32_t>(self, slot::kGetBuildingType, 1u));
    char* map = At<char*>(profile, 0xC);
    const uint32_t count = static_cast<uint32_t>((At<char*>(profile, 0x10) - map) / 12);
    uint32_t index = 0;
    char* found = nullptr;
    for (; index < count; ++index, map += 12) {
        char* const node = Find(map, type);
        if (node != At<char*>(map, 0x0)) {
            found = node;
            break;
        }
    }
    if (!found)
        return 0;
    const uint16_t capacity = At<uint16_t>(found, 0x14);
    char* const head = At<char*>(map, 0x0);
    char* node = At<char*>(head, 0x8);
    if (node == head)
        return capacity;
    float rest = 1.0f;
    for (; node != head; node = TreeNext(node)) {
        const uint32_t other = At<uint32_t>(node, 0x10);
        if (other == type)
            continue;
        char* const mine = At<char*>(self, field::kPopulations) + index * 12;
        char* const entry = Find(mine, other);
        if (entry == At<char*>(mine, 0x0))
            continue;
        const double share = static_cast<double>(At<uint16_t>(entry, 0x14)) / static_cast<double>(At<uint16_t>(node, 0x14));
        rest = static_cast<float>(static_cast<double>(rest) - share);
    }
    if (rest == 1.0f)
        return capacity;
    if (!At<uint8_t>(profile, 0x18))
        return 0;
    return static_cast<uint16_t>(RoundViaFloat(static_cast<double>(capacity) * static_cast<double>(rest)));
}

// Slot 60 (0x006BF660).
void __fastcall SetTravelDesignate(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("SetTravelDesignate", logged);
    void* const old = At<void*>(self, field::kDesignate);
    if (old == lot)
        return;
    if (lot)
        AddRefOf(lot);
    At<void*>(self, field::kDesignate) = lot;
    if (old)
        ReleaseOf(old);
}

// Slot 63 (0x006BF350): wealth 1 to 3; 0 for any other (ST0).
float __fastcall GetTravelJobs(char* self, void*, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetTravelJobs", logged);
    if (wealth >= 1 && wealth <= 3)
        return At<float>(self, field::kTravelJobs + 4 * (wealth - 1));
    return 0.0f;
}

// Slot 64 (0x006BF800): jobs[1..3] by wealth (jobs[0] is left alone).
// With a building type whose profile has a first value: the demand
// simulator's jobs per unit (slot 8, for the developer type of the lot's
// wealth plus 1) times the lot's population of that type. Otherwise from
// the building's properties 0x27812841 (pairs of key and count; keys
// 0x2010, 0x2020, 0x2030) and, when present, 0x27812843 (a float per pair
// in place of the count). EAX is what the game leaves there where it is
// simple to say (callers do not read it).
uint32_t __fastcall GetJobs(char* self, void*, float* jobs)
{
    static bool logged = false;
    FirstCall("GetJobs", logged);
    for (int i = 1; i < 4; ++i)
        At<uint32_t>(jobs, 4 * i) = 0;
    if (const uint32_t building = Virtual<uint32_t>(self, slot::kGetBuildingType, 1u)) {
        void* const development = Service(addr::SC4_Service43D68);
        const uint32_t first = *Virtual<uint32_t*>(development, 0x9C, building);
        if (first) {
            const uint32_t type = Virtual<uint32_t>(development, 0x78, first, uint32_t{At<uint8_t>(self, field::kWealth)});
            const uint16_t population = Virtual<uint16_t>(self, slot::kGetPopulation, type);
            Virtual<void>(Service(addr::SC4_Service43D74), 0x20, jobs, type + 1, 0u);
            const double factor = static_cast<double>(population);
            for (int i = 1; i < 4; ++i)
                jobs[i] = static_cast<float>(factor * static_cast<double>(jobs[i]));
            return population;
        }
    }
    void* const occupant = Virtual<void*>(self, slot::kGetBuilding);
    if (!occupant) {
        if (void* const config = At<void*>(self, field::kConfig))
            return Virtual<uint32_t>(config, 0xC);
        return 0;
    }
    void* const holder = Virtual<void*>(Virtual<void*>(occupant, 0xC), 0x18);
    if (!holder)
        return 0;
    void* const property = Virtual<void*>(holder, 0x24, 0x27812841u);
    if (!property)
        return 0;
    void* const value = Virtual<void*>(property, 0x14);
    const uint32_t* const pairs = Virtual<const uint32_t*>(value, 0x19C);
    const uint32_t count = Virtual<uint32_t>(value, 0x10);
    const float* factors = nullptr;
    if (void* const second = Virtual<void*>(holder, 0x24, 0x27812843u)) {
        void* const floats = Virtual<void*>(second, 0x14);
        factors = Virtual<const float*>(floats, 0x1BC);
        Virtual<uint32_t>(floats, 0x10);
    }
    for (uint32_t i = 0; i < count; i += 2) {
        const uint32_t key = pairs[i];
        const float amount = factors ? factors[(i + 1) / 2] : static_cast<float>(static_cast<double>(pairs[i + 1]));
        switch (key) {
        case 0x2010: jobs[1] = amount; break;
        case 0x2020: jobs[2] = amount; break;
        case 0x2030: jobs[3] = amount; break;
        default: break;
        }
    }
    return count;
}

// --- The business unit (cSC4BusinessUnit) -------------------------------------

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// Primary slot 0 (0x006BF390).
uint8_t __fastcall UnitQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("BusinessUnit::QueryInterface", logged);
    if (iid == kIidUnknown || iid == kIidBusinessUnit)
        *object = self;
    else if (iid == kIidSerializable)
        *object = self ? self + 0x4 : nullptr;
    else
        return 0;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// Primary slot 2 (0x006BFE40): at the last reference the holder part's
// (+0x8) slot 22, the deleting destructor.
uint32_t __fastcall UnitRelease(char* self, void*)
{
    static bool logged = false;
    FirstCall("BusinessUnit::Release", logged);
    uint32_t& count = At<uint32_t>(self, 0xC);
    if (count > 1)
        return --count;
    Virtual<void*>(self + 0x8, 0x58, 1u);
    return 0;
}

// Primary slot 3 (0x006C0940): the lot joins the list unless it is in it,
// and is told its unit either way. The references go up and down as the
// game's counted pointers do.
void __fastcall UnitAdd(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("BusinessUnit::Add", logged);
    ListNode* const end = At<ListNode*>(self, 0x1C);
    if (lot)
        AddRefOf(lot);
    ListNode* const head = At<ListNode*>(self, 0x1C);
    ListNode* node = head->next;
    while (node != head && node->value != lot)
        node = node->next;
    const bool missing = node == end;
    if (lot)
        ReleaseOf(lot);
    if (missing) {
        if (lot)
            AddRefOf(lot);
        ListNode* const last = At<ListNode*>(self, 0x1C);
        auto* const created = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
        created->value = lot;
        if (lot)
            AddRefOf(lot);
        ListNode* const previous = last->previous;
        created->next = last;
        created->previous = previous;
        previous->next = created;
        last->previous = created;
        if (lot)
            ReleaseOf(lot);
    }
    Virtual<void>(lot, slot::kSetUnit, static_cast<void*>(self));
}

// 0x006C0900: every node holding the lot erased (the game's list erase
// 0x007E9330, generic, drops the node's reference).
void UnitRemoveNodes(char* list, void* const* lot)
{
    ListNode* const head = At<ListNode*>(list, 0x0);
    for (ListNode* node = head->next; node != head;) {
        ListNode* const next = node->next;
        if (*lot == node->value) {
            void* out = nullptr;
            CallThis<void, void**, ListNode*>(addr::SC4RefList_Erase, list, &out, node);
        }
        node = next;
    }
}

// Primary slot 4 (0x006C08B0): the lot leaves the list, loses its unit and
// its travel designate.
void __fastcall UnitRemove(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("BusinessUnit::Remove", logged);
    void* held = lot;
    if (lot)
        AddRefOf(lot);
    UnitRemoveNodes(self + 0x1C, &held);
    if (held)
        ReleaseOf(held);
    Virtual<void>(lot, slot::kSetUnit, static_cast<void*>(nullptr));
    Virtual<void>(lot, slot::kSetTravelDesignate, static_cast<void*>(nullptr));
}

// +0x4 slot 3 (0x006C0370; `part` = unit + 4): version 1, the count and a
// reference per member lot.
uint8_t __fastcall UnitWrite(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("BusinessUnit::Write", logged);
    char* const self = part - 0x4;
    Virtual<uint8_t>(stream, 0x1C, 1u);
    ListNode* head = At<ListNode*>(self, 0x1C);
    uint32_t count = 0;
    for (ListNode* node = head->next; node != head; node = node->next)
        ++count;
    Virtual<uint8_t>(stream, 0x24, count);
    for (ListNode* node = At<ListNode*>(self, 0x1C)->next; node != At<ListNode*>(self, 0x1C); node = node->next)
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, node->value);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slot 23 (0x006C08A0): to Write with this - 4.
uint8_t __fastcall UnitWriteThunk(char* holder, void*, void* stream) { return UnitWrite(holder - 0x4, nullptr, stream); }

// --- Init, Shutdown, PostCityInit ----------------------------------------------

// Slot 5 (0x006BF490). Once: the travel jobs zeroed, the altitude taken
// from the terrain (slot 13) at the lot's centre and set through slot 17.
// A random 0-99 when +0x88 is unset. The configuration held is dropped;
// the configuration ID read from a save (+0x30) is resolved through the lot
// configuration manager (slot 8) and zeroed.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    // The guard byte of the game's static generator (nothing else is done
    // on the first pass).
    if (!Global<uint8_t>(addr::cSC4Lot_RandomGuard))
        Global<uint8_t>(addr::cSC4Lot_RandomGuard) = 1;
    if (!At<uint8_t>(self, field::kInitialized)) {
        for (std::size_t i = 0; i < 3; ++i)
            At<uint32_t>(self, field::kTravelJobs + 4 * i) = 0;
        void* const terrain = Service(addr::SC4_Terrain);
        float center[3] = {};
        Virtual<void*>(self, slot::kGetCenter, static_cast<float*>(center));
        const float altitude = static_cast<float>(Virtual<double>(terrain, 0x34, Bits(center[0]), Bits(center[2])));
        Virtual<void>(self, slot::kSetAltitude, altitude);
        At<uint8_t>(self, field::kInitialized) = 1;
    }
    if (At<uint8_t>(self, field::kRandom) == 0xFF)
        At<uint8_t>(self, field::kRandom) = static_cast<uint8_t>(
            CallThis<uint32_t, uint32_t>(addr::SC4Random_Below, reinterpret_cast<void*>(addr::cSC4Lot_Random), 100u));
    if (void* const config = At<void*>(self, field::kConfig)) {
        At<void*>(self, field::kConfig) = nullptr;
        ReleaseOf(config);
    }
    if (const uint32_t id = At<uint32_t>(self, field::kConfigId)) {
        void* const config = Virtual<void*>(Service(addr::SC4_Service43D18), 0x20, id);
        void* const old = At<void*>(self, field::kConfig);
        if (config != old) {
            if (config)
                AddRefOf(config);
            At<void*>(self, field::kConfig) = config;
            if (old)
                ReleaseOf(old);
        }
    }
    At<uint32_t>(self, field::kConfigId) = 0;
    return 1;
}

// Slot 7 (0x006C3060). Once initialized: the population maps destroyed
// (the vector keeps its memory), the travel population emptied, the
// configuration and the travel designate released, the business unit told
// to remove the lot (its slot 4).
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    uint8_t allocator = 0;
    char* const begin = At<char*>(self, field::kPopulations);
    CallCdecl<void, void*, void*, void*>(addr::SC4MapVector_DestroyRange, begin, At<char*>(self, field::kPopulations + 4),
                                         &allocator);
    At<char*>(self, field::kPopulations + 4) = begin;
    ClearTravel(self);
    if (void* const config = At<void*>(self, field::kConfig)) {
        At<void*>(self, field::kConfig) = nullptr;
        ReleaseOf(config);
    }
    if (void* const designate = At<void*>(self, field::kDesignate)) {
        At<void*>(self, field::kDesignate) = nullptr;
        ReleaseOf(designate);
    }
    if (void* const unit = At<void*>(self, field::kUnit)) {
        Virtual<void>(unit, 0x10, static_cast<void*>(self));
        At<void*>(self, field::kUnit) = nullptr;
    }
    return 1;
}

// Slot 8 (0x006BF200): the flags set again from 0 through slot 13, so the
// lot's cells are marked anew (the flags byte sign-extended, as the game's).
void __fastcall PostCityInit(char* self, void*)
{
    static bool logged = false;
    FirstCall("PostCityInit", logged);
    const int32_t flags = At<int8_t>(self, field::kFlags);
    At<uint8_t>(self, field::kFlags) = 0;
    Virtual<void>(self, slot::kSetFlags, flags);
}

// --- Save -------------------------------------------------------------------------

// +0x4 slot 3 (0x006C0080; `part` = lot + 4): version 8. The layout is in
// tools/re/save_schemas/citylot.py. False as soon as a trip record fails,
// else when the stream is in error after.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x4;
    const auto Byte = [&](std::size_t offset) { Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, offset)}); };
    const auto Dword = [&](std::size_t offset) { Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, offset)); };
    const auto Float = [&](std::size_t offset) { Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, offset)); };
    const auto Map = [&](char* map) {
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(map, 0x4)});
        for (char* node = At<char*>(At<char*>(map, 0x0), 0x8); node != At<char*>(map, 0x0); node = TreeNext(node)) {
            const uint16_t value = At<uint16_t>(node, 0x14);
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x10));
            Virtual<uint8_t>(stream, 0x1C, uint32_t{value});
        }
    };
    Virtual<uint8_t>(stream, 0x1C, 8u);
    void* const config = At<void*>(self, field::kConfig);
    Virtual<uint8_t>(stream, 0x24, config ? Virtual<uint32_t>(config, 0xC) : 0u);
    Byte(field::kFlags);
    for (const std::size_t offset : {field::kMinX, field::kMinZ, field::kMaxX, field::kMaxZ, field::kCellX, field::kCellZ})
        Byte(offset);
    Float(field::kAltitude);
    Float(field::kBuiltSlope);
    Float(field::kSlope);
    for (std::size_t offset = field::kWidth; offset <= field::kWealth; ++offset)
        Byte(offset);
    Dword(field::kStateDay);
    Dword(field::kBuildingType);
    Byte(field::kVacateReason);
    CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kUnit));
    CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kDesignate));
    const int32_t maps =
        static_cast<int32_t>((At<char*>(self, field::kPopulations + 4) - At<char*>(self, field::kPopulations)) / 12);
    Virtual<uint8_t>(stream, 0x14, maps);
    for (char* map = At<char*>(self, field::kPopulations); map != At<char*>(self, field::kPopulations + 4); map += 12)
        Map(map);
    Map(self + field::kTravel);
    for (std::size_t i = 0; i < 3; ++i)
        Float(field::kTravelJobs + 4 * i);
    CallThis<uint8_t, void*>(addr::SC4PropertyHolder_Write, self + field::kHolder, stream);
    ListNode* const trips = At<ListNode*>(self, field::kTrips);
    uint32_t count = 0;
    for (ListNode* node = trips->next; node != trips; node = node->next)
        ++count;
    Virtual<uint8_t>(stream, 0x24, count);
    ListNode* const head = At<ListNode*>(self, field::kTrips);
    for (ListNode* node = head->next; node != head; node = node->next)
        if (!CallThis<uint8_t, void*>(addr::SC4TripRecord_Write, &node->value, stream))
            return 0;
    Byte(field::kRandom);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slot 23 (0x006C2330): to Write with this - 4.
uint8_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }

// --- Installation ---------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 11, addr::cSC4Lot_GetLotConfigurationObject, GetLotConfigurationObject),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 13, addr::cSC4Lot_SetFlags, SetFlags),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 14, addr::cSC4Lot_GetBoundingRect, GetBoundingRect),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 15, addr::cSC4Lot_GetCenter, GetCenter),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 17, addr::cSC4Lot_SetAltitude, SetAltitude),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 20, addr::cSC4Lot_UpdateSlope, UpdateSlope),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 21, addr::cSC4Lot_GetLocation, GetLocation),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 22, addr::cSC4Lot_GetSize, GetSize),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 27, addr::cSC4Lot_HasJobAccess, HasJobAccess),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 32, addr::cSC4Lot_UpdateZoneType, UpdateZoneType),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 40, addr::cSC4Lot_GetDaysSinceLastStateChange, GetDaysSinceLastStateChange),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 42, addr::cSC4Lot_GetLandValue, GetLandValue),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 46, addr::cSC4Lot_GetBuildingType, GetBuildingType),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 48, addr::cSC4Lot_GetBuilding, GetBuilding),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 49, addr::cSC4Lot_GetLotOccupants, GetLotOccupants),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 50, addr::cSC4Lot_GetCapacityByType, GetCapacityByType),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 51, addr::cSC4Lot_GetCapacityByWealth, GetCapacityByWealth),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 52, addr::cSC4Lot_GetAvailableCapacity, GetAvailableCapacity),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 53, addr::cSC4Lot_GetPopulation, GetPopulation),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 56, addr::cSC4Lot_GetTravelPopulation, GetTravelPopulation),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 57, addr::cSC4Lot_ResetTravelPopulation, ResetTravelPopulation),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 60, addr::cSC4Lot_SetTravelDesignate, SetTravelDesignate),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 63, addr::cSC4Lot_GetTravelJobs, GetTravelJobs),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 64, addr::cSC4Lot_GetJobs, GetJobs),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable, 0, addr::cSC4BusinessUnit_QueryInterface, UnitQueryInterface),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable, 2, addr::cSC4BusinessUnit_Release, UnitRelease),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable, 3, addr::cSC4BusinessUnit_Add, UnitAdd),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable, 4, addr::cSC4BusinessUnit_Remove, UnitRemove),
};
const Slot kInitSlots[] = {
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 5, addr::cSC4Lot_Init, Init),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 7, addr::cSC4Lot_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 8, addr::cSC4Lot_PostCityInit, PostCityInit),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4Lot_vftable_4, 3, addr::cSC4Lot_Write, Write),
    OPENSC4_SLOT(addr::cSC4Lot_vftable_8, 23, addr::cSC4Lot_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable_4, 3, addr::cSC4BusinessUnit_Write, UnitWrite),
    OPENSC4_SLOT(addr::cSC4BusinessUnit_vftable_8, 23, addr::cSC4BusinessUnit_WriteThunk, UnitWriteThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("citylot %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("citylot %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

using SelfFn = void(__fastcall*)(char*, void*);
SelfFn g_originalPushTravelJobs = nullptr;
SelfFn g_originalUpdateNeedsPower = nullptr;

// Our 0x006BF6A0, installed once for whichever of simulators.citylot.slots
// and fixes.travel_jobs comes first.
bool InstallPushTravelJobs()
{
    static bool installed = false;
    if (installed)
        return true;
    const addr::Entry* const push = addr_check::Find(addr::cSC4Lot_PushTravelJobs);
    installed = push && hook::Install(*push, &PushTravelJobs, &g_originalPushTravelJobs);
    return installed;
}

// The slots, and detours on the two functions of this range that the
// other parts' code calls directly: 0x006BF6A0 (from AddTravelPopulation's
// end 0x006C0CE1, which only pops and returns after) and 0x006BFD20 (from
// SetState 0x006C3417, which reloads every register it uses after). No
// caller keeps a volatile register across them.
bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const power = addr_check::Find(addr::cSC4Lot_UpdateNeedsPower);
    if (!power || !SwapSlots(kSlots, "slots"))
        return false;
    return InstallPushTravelJobs() && hook::Install(*power, &UpdateNeedsPower, &g_originalUpdateNeedsPower);
}

// fixes.travel_jobs: our 0x006BF6A0 alone, asking each capacity's own value
// (docs/citylot.md, "Travel jobs").
bool InstallTravelJobsFix(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    g_fixTravelJobs = true;
    const bool ok = InstallPushTravelJobs();
    log::Write("travel jobs fix: %s", ok ? "on" : "NOT installed");
    if (!ok)
        g_fixTravelJobs = false;
    return ok;
}

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.citylot.slots", "simulators/citylot", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4Lot_vftable,
                        "Our cSC4Lot slots of 0x006BF180-0x006C09E0 (24 slots, two helpers by detour) and "
                        "cSC4BusinessUnit's QueryInterface, Release, Add and Remove (src/core/citylot_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.citylot.init", "simulators/citylot", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4Lot_vftable,
                        "Our cSC4Lot Init, Shutdown and PostCityInit (src/core/citylot_sim.h)", false, InstallInit}),
    registry::Register({"simulators.citylot.save", "simulators/citylot", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4Lot_vftable_4,
                        "Our cSC4Lot Write (the save record, version 8) and cSC4BusinessUnit Write "
                        "(src/core/citylot_sim.h)",
                        false, InstallSave}),
    registry::Register({"fixes.travel_jobs", "fixes", registry::Kind::Detour, registry::Phase::PostAppInit,
                        addr::cSC4Lot_PushTravelJobs,
                        "Lots offer their $$ and $$$ jobs when the count is close to the $ jobs (the game "
                        "skipped them, so high-tech industry had no $$$ jobs)",
                        false, InstallTravelJobsFix,
                        true, true}), // a fix of the original's bug: always on (registry.h)
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's four vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups and parts.
constexpr int kSlots0 = 70, kSlots4 = 6, kSlots8 = 25, kSlots1C = 6;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtable1C[kSlots1C];

// As the game's (0x004664A0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006BF190).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == kIidUnknown || iid == kIidLot)
        *object = self;
    else if (iid == kIidSerializable)
        *object = self ? self + field::kSerializable : nullptr;
    else
        return 0;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006BF460): Shutdown and the holder part's slot 22 (the
// deleting destructor) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    uint32_t& count = At<uint32_t>(self, field::kRefCount);
    if (count > 1)
        return --count;
    Virtual<uint8_t>(self, slot::kShutdown);
    Virtual<void*>(self + field::kHolder, 0x58, 1u);
    return 0;
}

// As the game's (0x006BF180).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kSerializable) = g_vtable4;
    At<void**>(self, field::kHolder) = g_vtable8;
    At<void**>(self, field::kTrafficSource) = g_vtable1C;
}

// The destructor (0x006C25D0), in the game's order: the trip list, the
// travel population, the population maps, the travel designate, the tree
// +0x40, the configuration, the holder.
void Destroy(char* self)
{
    SetVtables(self);
    CallThis<void>(addr::SC4TripList_Clear, self + field::kTrips);
    if (void* const head = At<void*>(self, field::kTrips))
        Free(head);
    ClearTravel(self);
    if (void* const head = At<void*>(self, field::kTravel))
        Free(head);
    uint8_t allocator = 0;
    CallCdecl<void, void*, void*, void*>(addr::SC4MapVector_DestroyRange, At<void*>(self, field::kPopulations),
                                         At<void*>(self, field::kPopulations + 4), &allocator);
    if (void* const maps = At<void*>(self, field::kPopulations))
        Free(maps);
    if (void* const designate = At<void*>(self, field::kDesignate))
        ReleaseOf(designate);
    char* const tree = self + field::kMap40;
    if (At<uint32_t>(tree, 0x4)) {
        EraseNodes(At<char*>(At<char*>(tree, 0x0), 0x4));
        ResetHead(tree);
    }
    if (void* const head = At<void*>(tree, 0x0))
        Free(head);
    if (void* const config = At<void*>(self, field::kConfig))
        ReleaseOf(config);
    CallThis<void>(addr::SC4PropertyHolder_Destroy, self + field::kHolder);
}

// The deleting destructor (0x006C3000): the memory goes back to the lot
// pool 0x00B0D3AC (the block header is at the address with its low 14 bits
// clear: free list +0x8, used count +0xC).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if ((flags & 1) && self) {
        char* const block = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFC000u);
        At<void*>(self, 0x0) = At<void*>(block, 0x8);
        const uint32_t used = At<uint32_t>(block, 0xC) - 1;
        At<void*>(block, 0x8) = self;
        At<uint32_t>(block, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4Lot_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, block);
        else if (used == 0x40000057)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, block);
    }
    return self;
}

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

// The holder part's slot 22 (0x006C22A0).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4Lot_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4Lot_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4Lot_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtable1C, reinterpret_cast<const void*>(addr::cSC4Lot_vftable_1C), sizeof(g_vtable1C));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable4[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable1C[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x1C>);
    g_vtable1C[1] = reinterpret_cast<void*>(&AddRefAt<0x1C>);
    g_vtable1C[2] = reinterpret_cast<void*>(&ReleaseAt<0x1C>);
    log::Write("citylot object: vtables copied (slots 5/34 %p %p, +4 slots 3/4 %p %p, +8 slots 23/24 %p %p)",
               g_vtable[5], g_vtable[34], g_vtable4[3], g_vtable4[4], g_vtable8[23], g_vtable8[24]);
}

void EnsureVtables()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
}

void LogConstructed(const char* how, char* self)
{
    static int created = 0;
    if (created++ < 3)
        log::Write("citylot object: constructed %p (%s)", static_cast<void*>(self), how);
}

// What the default and the cell constructors share, in the game's order of
// allocations: the holder base (0x005FF100 inline: four zero dwords), the
// fields zeroed, the tree +0x40, the travel map +0x60, the trip list's
// head (0x34 bytes, a whole node) and the unset random byte. +0x20-+0x23,
// +0x48, +0x68 and the bytes named below are left as they are.
void ConstructCommon(char* self, bool all)
{
    EnsureVtables();
    SetVtables(self);
    for (std::size_t offset = 0xC; offset <= 0x18; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = 0x24; offset <= 0x3C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    NewTree(self + field::kMap40);
    for (std::size_t offset = 0x4C; offset <= 0x5C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    NewTree(self + field::kTravel);
    for (std::size_t offset = 0x6C; offset <= 0x74; offset += 4)
        At<uint32_t>(self, offset) = 0;
    // The cell constructor sets the facing later and leaves the vacate
    // reason +0x83 unset; the default one zeroes both.
    for (std::size_t offset = 0x78; offset <= 0x83; ++offset)
        if (all || (offset != field::kFacing && offset != field::kVacateReason))
            At<uint8_t>(self, offset) = 0;
    At<void*>(self, field::kTrips) = nullptr;
    auto* const head = static_cast<ListNode*>(Allocate(0x34));
    head->next = head;
    head->previous = head;
    At<ListNode*>(self, field::kTrips) = head;
    At<uint8_t>(self, field::kRandom) = 0xFF;
}

// The cell constructor (detour of 0x006C1E50; the lot manager's CreateLot
// 0x0072E557): the rectangle from the corner and the size, then the lot's
// own cell and its size across and deep by the facing, and the zone type
// at that cell.
void* __fastcall Construct(char* self, void*, int32_t x, int32_t z, int32_t width, int32_t depth, uint32_t facing)
{
    ConstructCommon(self, false);
    const uint8_t maxX = static_cast<uint8_t>(x + width - 1), maxZ = static_cast<uint8_t>(z + depth - 1);
    At<uint8_t>(self, field::kMinX) = static_cast<uint8_t>(x);
    At<uint8_t>(self, field::kMinZ) = static_cast<uint8_t>(z);
    At<uint8_t>(self, field::kMaxX) = maxX;
    At<uint8_t>(self, field::kMaxZ) = maxZ;
    At<uint8_t>(self, field::kFacing) = static_cast<uint8_t>(facing);
    const auto Place = [&](uint8_t cellX, uint8_t cellZ, int32_t across, int32_t deep) {
        At<uint8_t>(self, field::kCellX) = cellX;
        At<uint8_t>(self, field::kCellZ) = cellZ;
        At<uint8_t>(self, field::kWidth) = static_cast<uint8_t>(across);
        At<uint8_t>(self, field::kDepth) = static_cast<uint8_t>(deep);
    };
    switch (facing & 0xFF) {
    case 0: Place(maxX, static_cast<uint8_t>(z), width, depth); break;
    case 1: Place(maxX, maxZ, depth, width); break;
    case 2: Place(static_cast<uint8_t>(x), maxZ, width, depth); break;
    case 3: Place(static_cast<uint8_t>(x), static_cast<uint8_t>(z), depth, width); break;
    default: break;
    }
    At<uint8_t>(self, field::kZoneType) =
        Virtual<uint8_t>(Service(addr::SC4_ZoneManager), 0x30, uint32_t{At<uint8_t>(self, field::kCellX)},
                         uint32_t{At<uint8_t>(self, field::kCellZ)});
    LogConstructed("cells", self);
    return self;
}

// The default constructor (detour of 0x006C2340; the factory 0x006C2FC0,
// when a save is read).
void* __fastcall ConstructDefault(char* self, void*)
{
    ConstructCommon(self, true);
    LogConstructed("default", self);
    return self;
}

// The copy constructor (detour of 0x006C2450; the snapshots SetCondition
// and SetState send with their messages): the holder copied by the game's
// 0x005FF120, the fields, the configuration referenced, the population
// maps and the travel map copied by the game's container code; the tree
// +0x40, the business unit, the travel designate and the trips are not
// copied.
void* __fastcall ConstructCopy(char* self, void*, char* other)
{
    EnsureVtables();
    CallThis<void*, void*>(addr::SC4PropertyHolder_CopyConstruct, self + field::kHolder,
                           other ? other + field::kHolder : nullptr);
    SetVtables(self);
    for (std::size_t offset = 0x20; offset <= 0x30; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(other, offset);
    void* const config = At<void*>(other, field::kConfig);
    At<void*>(self, field::kConfig) = config;
    if (config)
        AddRefOf(config);
    At<uint32_t>(self, field::kStateDay) = At<uint32_t>(other, field::kStateDay);
    At<uint32_t>(self, field::kBuildingType) = At<uint32_t>(other, field::kBuildingType);
    NewTree(self + field::kMap40);
    At<void*>(self, field::kUnit) = nullptr;
    At<void*>(self, field::kDesignate) = nullptr;
    CallThis<void*, void*>(addr::SC4MapVector_CopyConstruct, self + field::kPopulations, other + field::kPopulations);
    CallThis<void*, void*>(addr::SC4LotTravelMap_CopyConstruct, self + field::kTravel, other + field::kTravel);
    for (std::size_t offset = 0x6C; offset <= 0x74; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(other, offset);
    for (std::size_t offset = 0x78; offset <= 0x83; ++offset)
        At<uint8_t>(self, offset) = At<uint8_t>(other, offset);
    At<void*>(self, field::kTrips) = nullptr;
    auto* const head = static_cast<ListNode*>(Allocate(0x34));
    head->next = head;
    head->previous = head;
    At<ListNode*>(self, field::kTrips) = head;
    At<uint8_t>(self, field::kRandom) = At<uint8_t>(other, field::kRandom);
    LogConstructed("copy", self);
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*, int32_t, int32_t, int32_t, int32_t, uint32_t);
using ConstructDefaultFn = void*(__fastcall*)(char*, void*);
using ConstructCopyFn = void*(__fastcall*)(char*, void*, char*);
ConstructFn g_originalConstruct = nullptr;
ConstructDefaultFn g_originalConstructDefault = nullptr;
ConstructCopyFn g_originalConstructCopy = nullptr;

// Every construction path: no caller keeps a volatile register across the
// constructors (0x0072E55C, 0x006C2741 and 0x006C344E read only EAX after;
// the factory jumps to the default one).
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const cells = addr_check::Find(addr::cSC4Lot_cSC4Lot);
    const addr::Entry* const plain = addr_check::Find(addr::cSC4Lot_DefaultConstruct);
    const addr::Entry* const copy = addr_check::Find(addr::cSC4Lot_CopyConstruct);
    return cells && plain && copy && hook::Install(*cells, &Construct, &g_originalConstruct) &&
           hook::Install(*plain, &ConstructDefault, &g_originalConstructDefault) &&
           hook::Install(*copy, &ConstructCopy, &g_originalConstructCopy);
}

const bool g_registeredObject = registry::Register({
    "simulators.citylot.object",
    "simulators/citylot",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Lot_cSC4Lot,
    "Our cSC4Lot object: the three constructors (detours), destructor, identity; methods from copies of the "
    "(patched) game vtables taken once (src/core/citylot_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::citylot_sim
