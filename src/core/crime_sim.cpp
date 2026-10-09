// Our cSC4CrimeSimulator (build 641), first part: its interface, swapped into
// the original object's vtable (docs/crime.md). The getters read the crime
// grid (+0x5C), the hotspot (+0x80 to +0x88) and the incidents of the month
// (+0x70); the nearest queries go through the occupant manager, as the
// game's do.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::crime_sim {
namespace {

// Fields of cSC4CrimeSimulator used here (offsets from the object's start).
namespace field {
constexpr std::size_t kCrimeGrid = 0x5C;      // cISC4SimGrid<uint8_t>*
constexpr std::size_t kAverageCrime = 0x6C;   // float
constexpr std::size_t kIncidents = 0x70;      // vector of Incident*: begin, end, capacity
constexpr std::size_t kMonthlyCrimes = 0x7C;
constexpr std::size_t kHotspotX = 0x80;
constexpr std::size_t kHotspotZ = 0x84;
constexpr std::size_t kHotspotValue = 0x88;
} // namespace field

// An incident of the month (0x2C bytes).
struct Incident {
    void* occupant;       // cISC4Occupant*, referenced
    uint32_t crimeType;
    void* nameVtable;     // a string: vtable, then its buffer
    char* nameBegin;
    char* nameEnd;
    char* nameCapacity;
    uint32_t value18;
    uint32_t key[3];      // type, group, instance
    uint8_t visualsCreated;
};
static_assert(offsetof(Incident, key) == 0x1C);
static_assert(offsetof(Incident, visualsCreated) == 0x28);

// cISC4CrimeSimulator::tCrimeEvent (gzcom-dll).
struct CrimeEvent {
    int32_t cellX;
    int32_t cellZ;
    uint32_t crimeType;
    uint8_t visualsCreated;
    uint32_t key[3];
};
static_assert(sizeof(CrimeEvent) == 0x1C);

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

using OperatorNewFn = void*(__cdecl*)(std::size_t size);
using FilterBaseFn = void(__thiscall*)(void* filter);
using ListDestroyFn = void(__thiscall*)(void* list);
using ResizeEventsFn = void(__thiscall*)(void* vector, uint32_t count, const CrimeEvent* fill);
const auto OperatorNew = reinterpret_cast<OperatorNewFn>(addr::SC4_operator_new);
const auto FilterBase = reinterpret_cast<FilterBaseFn>(addr::SC4_OccupantFilterBase);
const auto ListDestroy = reinterpret_cast<ListDestroyFn>(addr::SC4List_Destroy2);
const auto ResizeEvents = reinterpret_cast<ResizeEventsFn>(addr::SC4Vector28_Resize);

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void* Grid(void* self) { return At<void*>(self, field::kCrimeGrid); }

Incident** Incidents(void* self, uint32_t* count)
{
    auto& incidents = At<Vector>(self, field::kIncidents);
    *count = static_cast<uint32_t>((incidents.end - incidents.begin) >> 2);
    return reinterpret_cast<Incident**>(incidents.begin);
}

// --- The occupant filters of the nearest queries -----------------------------

// The game's filters are 12 bytes: a vtable, a second one at +4 and a
// reference count. Ours copy the game's vtables but for the test (slot 3).
constexpr int kFilterSlots = 8;
std::uintptr_t g_propFilterVtable[kFilterSlots];
std::uintptr_t g_simFilterVtable[kFilterSlots];

// 0x0069F490: a prop (0xA823821E) whose exemplar has 0x8AA864D5.
uint8_t __fastcall IsCrimeProp(void*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != 0xA823821E)
        return 0;
    return Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, 0x8AA864D5u) ? 1 : 0;
}

// 0x0069F4D0: an automaton (0x496E5783) with flag 0x4102.
uint8_t __fastcall IsCrimeSim(void*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != 0x496E5783)
        return 0;
    return Virtual<uint8_t>(occupant, 0x58, 0x4102u) ? 1 : 0;
}

void MakeFilterVtable(std::uintptr_t* ours, std::uintptr_t game, void* test)
{
    std::memcpy(ours, reinterpret_cast<const void*>(game), sizeof(std::uintptr_t) * kFilterSlots);
    ours[3] = reinterpret_cast<std::uintptr_t>(test);
}

void* NewFilter(const std::uintptr_t* vtable, std::uintptr_t secondVtable)
{
    void* const filter = OperatorNew(0xC);
    if (!filter)
        return nullptr;
    FilterBase(filter);
    At<const void*>(filter, 0) = vtable;
    At<std::uintptr_t>(filter, 4) = secondVtable;
    Virtual<uint32_t>(filter, 0x4); // AddRef
    return filter;
}

// 0x006A10F0 and 0x006A0E50: the occupants that pass `filter` in a box of
// `radius` around the position (all heights), the nearest within the radius
// that has interface `iid` (and for props, whose slot 12 is 0). The pointer is
// returned after its reference from QueryInterface is released, as the game
// does.
void* Nearest(const float* position, float radius, const std::uintptr_t* vtable, std::uintptr_t secondVtable,
              uint32_t iid, bool propCheck)
{
    const float boxMax[3] = {radius + position[0], 3.4028235e38f, radius + position[2]};
    const float boxMin[3] = {position[0] - radius, -3.4028235e38f, position[2] - radius};
    void* const occupants = Virtual<void*>(Global<void*>(addr::SC4_City), 0x88);
    if (occupants)
        Virtual<uint32_t>(occupants, 0x4);
    float where[3];
    const uint32_t start = Global<uint32_t>(addr::SC4_VectorDefault2);
    std::memcpy(&where[0], &start, 4);
    std::memcpy(&where[1], &start, 4);
    std::memcpy(&where[2], &start, 4);
    void* const filter = NewFilter(vtable, secondVtable);
    void* const allocator = ListNodeAllocator();
    void** const list = Virtual<void**>(allocator, 0xC, 0xCu);
    list[0] = list;
    list[1] = list;
    void* head = list;
    float best = radius;
    void* nearest = nullptr;
    if (Virtual<uint8_t>(occupants, 0x90, &head, boxMin, boxMax, filter, -1)) {
        void** const sentinel = static_cast<void**>(head);
        for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
            void* const occupant = node[2];
            Virtual<uint32_t>(occupant, 0x20, where);
            // The differences are stored back as floats; the distance is
            // computed from the unrounded ones and compared before rounding.
            const double dx = static_cast<double>(where[0]) - position[0];
            const double dy = static_cast<double>(where[1]) - position[1];
            const double dz = static_cast<double>(where[2]) - position[2];
            where[0] = static_cast<float>(dx);
            where[1] = static_cast<float>(dy);
            where[2] = static_cast<float>(dz);
            const double distance = std::sqrt(dy * dy + dz * dz + dx * dx);
            if (!(distance < static_cast<double>(best)))
                continue;
            void* found = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, iid, &found) && (!propCheck || Virtual<uint32_t>(found, 0x30) == 0)) {
                best = static_cast<float>(distance);
                nearest = found;
            }
            if (found)
                Virtual<uint32_t>(found, 0x8);
        }
        Virtual<uint8_t>(occupants, 0xC0, &head);
    }
    ListDestroy(&head);
    if (filter)
        Virtual<uint32_t>(filter, 0x8);
    Virtual<uint32_t>(occupants, 0x8);
    return nearest;
}

// --- The getters -------------------------------------------------------------

// 5 (0x006A0050).
void* __fastcall GetCrimeGrid(void* self, void*)
{
    return Grid(self);
}

// 6 (0x0069F350): the float, truncated.
uint32_t __fastcall GetAverageCrime(void* self, void*)
{
    return static_cast<uint32_t>(Ftol(At<float>(self, field::kAverageCrime)));
}

// 7 (0x00407BA0, a getter shared with other classes).
uint32_t __fastcall GetMonthlyCrimeCount(void* self, void*)
{
    return At<uint32_t>(self, field::kMonthlyCrimes);
}

// 8 (0x0069F430): the hotspot's value and its position in cells times the
// grid's cell size (grid slot 10).
uint8_t __fastcall GetCrimeHotspot(void* self, void*, uint8_t* value, int32_t* x, int32_t* z)
{
    if (At<int32_t>(self, field::kHotspotValue) <= 0)
        return 0;
    if (value)
        *value = At<uint8_t>(self, field::kHotspotValue);
    if (x)
        *x = Virtual<int32_t>(Grid(self), 0x28) * At<int32_t>(self, field::kHotspotX);
    if (z)
        *z = Virtual<int32_t>(Grid(self), 0x28) * At<int32_t>(self, field::kHotspotZ);
    return 1;
}

// 9 (0x006A10F0).
void* __fastcall GetNearestCrimeSim(void*, void*, const float* position, float radius)
{
    return Nearest(position, radius, g_simFilterVtable, 0x00AB0D34, 0xA9B40F05, false);
}

// 10 (0x006A0E50).
void* __fastcall GetNearestCrimeProp(void*, void*, const float* position, float radius)
{
    return Nearest(position, radius, g_propFilterVtable, 0x00AB0D00, 0xE9793A65, true);
}

// 11 (0x006A1960): one event per incident, in the caller's vector sized to
// the count (elements past those it already had are copies of a default
// whose first 16 bytes the game leaves uninitialised; ours are zero). An
// incident without an occupant leaves its element as it was.
uint8_t __fastcall GetCrimeIncidentArray(void* self, void*, Vector* events)
{
    uint32_t count;
    Incident** const incidents = Incidents(self, &count);
    void* const city = Global<void*>(addr::SC4_City);
    const CrimeEvent fill = {};
    ResizeEvents(events, count, &fill);
    for (uint32_t i = 0; i < count; ++i) {
        const Incident* const incident = incidents[i];
        if (!incident || !incident->occupant)
            continue;
        float position[3];
        Virtual<uint32_t>(incident->occupant, 0x3C, position);
        auto* const event = reinterpret_cast<CrimeEvent*>(events->begin) + i;
        Virtual<uint8_t>(city, 0x17C, position[0], position[2], &event->cellX, &event->cellZ);
        std::memcpy(event->key, incident->key, sizeof event->key);
        event->crimeType = incident->crimeType;
        event->visualsCreated = incident->visualsCreated;
    }
    return count > 0 ? 1 : 0;
}

// 12 (0x0069FD40).
uint8_t __fastcall SetVisualsCreated(void* self, void*, uint32_t index, uint8_t created)
{
    uint32_t count;
    Incident** const incidents = Incidents(self, &count);
    if (index >= count)
        return 0;
    incidents[index]->visualsCreated = created;
    return 1;
}

// 13 (0x0069F9A0).
uint8_t __fastcall GetVisualsCreated(void* self, void*, uint32_t index)
{
    uint32_t count;
    Incident** const incidents = Incidents(self, &count);
    return index < count ? incidents[index]->visualsCreated : 0;
}

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x006A0050, GetCrimeGrid),
    OPENSC4_SLOT(6, 0x0069F350, GetAverageCrime),
    OPENSC4_SLOT(7, 0x00407BA0, GetMonthlyCrimeCount),
    OPENSC4_SLOT(8, 0x0069F430, GetCrimeHotspot),
    OPENSC4_SLOT(9, 0x006A10F0, GetNearestCrimeSim),
    OPENSC4_SLOT(10, 0x006A0E50, GetNearestCrimeProp),
    OPENSC4_SLOT(11, 0x006A1960, GetCrimeIncidentArray),
    OPENSC4_SLOT(12, 0x0069FD40, SetVisualsCreated),
    OPENSC4_SLOT(13, 0x0069F9A0, GetVisualsCreated),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 15;

// Swaps every slot or none: each must still hold the game's function.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4CrimeSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("crime interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    MakeFilterVtable(g_propFilterVtable, 0x00AB0D18, reinterpret_cast<void*>(&IsCrimeProp));
    MakeFilterVtable(g_simFilterVtable, 0x00AB0D4C, reinterpret_cast<void*>(&IsCrimeSim));
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("crime interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("crime interface: %u of %d cISC4CrimeSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.crime.interface",
    "simulators/crime",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CrimeSimulator_vftable,
    "Our cISC4CrimeSimulator getters on the original object (docs/crime.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::crime_sim
