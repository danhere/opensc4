// Our cSC4LotDeveloper's construct part (build 641; class ID 0xA990BFE0): the
// class's code from 0x006CB4D0 to its end at 0x006CDEED, without Init
// (0x006CB9A0), Read (0x006CD120) and DoMessage (0x006CD940), which are the
// object part's (src/core/lot_object.cpp; the class map is in lot_sim.h).
//
// Slots of the primary vtable 0x00AB1B98, swapped by simulators.lot.construct:
//   6  0x006CC490  StartConstruction (lot, configuration, building type, keep)
//   7  0x006CCEF0  ConstructDefaultLot (the same; chooses a building type)
//   11 0x006CB940  RemoveBuildingZotProp (lot)
//   12 0x006CC1D0  StartDemolishLot (lot)
//   14 0x006CC250  ExplodeLot (lot, burnt)
//   34 0x006CB4D0  UpdateTravelDesignates (business unit*&)
//   37 0x006CC880  UpdateLotElements (lot, reason)
//   43 0x006CC970  SetEmptyLotBaseTexture (lot)
//   64 0x006CB760  GetLotConfigurationObjectOccupant (lot, index, occupant*&)
// Nothing in the exe calls these by address: the only references are the
// vtable's.
//
// Functions without a slot (names ours). The first four and the last are
// called by the object part's DoMessage, so the group detours them; their
// call sites are plain thiscall calls that keep nothing in ECX, EDX or EAX
// (each is followed by pops and the return, or by reloads from EDI/ESI/EBP):
//   0x006CBD60  StepNightBuildings (ms): from 0x006CDCB7, message 0x86AD10EE
//   0x006CBE50  FinishNightBuildings (state): from 0x006CD9C2, 0x006CDA86
//   0x006CBF40  UpdatePowerInRect (rect): from 0x006CDC79, message 0x8A07AE00
//   0x006CD2B0  PlantLotFlora (lot, configuration, bool): from 0x006CDEC8
//   0x006CD770  StepLotUpdate (): from 0x006CDB82, message 0x66956814
//   0x006CD010  ChooseFloraType: only from PlantLotFlora (plain function)
//   0x006CD3A0  UpdateLot (lot): only from StepLotUpdate (plain function)
// Left the game's: 0x006CCE30, the building deque's resize (STL code, only
// called by Read).
//
// What the functions do.
//
// Construction (StartConstruction). The lot is given the configuration
// (lot slot 10), the building type, state 2 and condition 1, a business
// unit (0x006CB260), flags 1, 2 and 4 cleared and its vertices locked; a
// zot of kind 6 is taken away. With a building type:
//   - the configuration's flag (its slot 14) and limit (slot 10, a float):
//     a flagged configuration with limit 0 gets a lot foundation at once
//     (slot 54: lot, configuration, 0, 1, 1);
//   - otherwise, for buildings whose profile (development simulator slot
//     39) does not start with 5: with tuning +0xE1 a lot wider and deeper
//     than one cell has its inner vertices levelled (slot 19); when the
//     lot's slope (slot 30) is above +0xD4 and above the limit, with
//     tuning +0xE0 the area is levelled to the limit (slot 21) and the
//     slope taken again; a slope still above the limit gets the lot
//     foundation;
//   - the lot's objects (0x006C9AD0), the building's place (slot 40) and
//     the building (development simulator slot 7). No building: false;
//   - without a lot foundation: when the building's own slope (slot 31) is
//     above +0xD4, its cells (slot 22) are levelled to +0xD4 (slot 21) and
//     if the slope stays above, the building gets a foundation (slot 58);
//   - with `keep` a construction occupant (class 0xA97F909E from the COM,
//     else made here) is given the lot and inserted in the occupant
//     manager; without it the lot goes on to state 3.
// ConstructDefaultLot first chooses the type when none is given: the
// configuration's building object (its slot 24) lists candidates, which
// are types (development simulator slot 15) or families whose types its
// slot 19 adds; all go in a hash set and the set's first entry is taken.
//
// Demolition. StartDemolishLot clears the flora in the lot's rectangle
// (flora simulator slot 20) when the lot's flag 1 is set, tells the prop
// manager (slot 28), clears the zot property and sets state 5 through
// 0x006C3C50. ExplodeLot (not for states 5 and 6) leaves, for a lot with a
// building, an effect object on the building's box and a crater object of
// twice the box's larger side with one of two IDs (burnt or not), both
// through the prop manager's slot 25, then 0x006C4BD0 (debris); the lot
// goes to state 6, condition 6 when burnt, building type 0.
//
// Lot elements (UpdateLotElements), for lots in state 2 or more: lots with
// flag 1 or 2 have foundation and retaining walls updated (slots 61, 62);
// others (unless the reason is 5) get a foundation when the
// configuration's limit is below the lot's slope, and 0x006C91F0 when the
// slope is above 0.25. Then the base texture occupant (slot 45) and the
// lot's slope (lot slot 20).
//
// Empty lot texture (SetEmptyLotBaseTexture). Zone types 0, 11, 12 and 15
// go to 0x006CAAF0. Otherwise each cell of the lot inside the city gets a
// 16-byte record: for each of the four sides 0 when the neighbour cell is
// the same lot, 2 when the lot faces that side (north z - 1: facing 0,
// east x + 1: 1, south z + 1: 2, west x - 1: 3), else 1; slot 42 turns the
// four into a texture ID, an orientation and a flag. 1 x 1 lots of zone
// types 7-9 pass four zeros. The record also has the zone's drag colour
// with the zone display alpha. The records go to the lot's base texture
// occupant (slot 41 with create = 1; its slot 8), which is inserted in the
// occupant manager, or moved and told 0x99EF1145 when its flag 1 is set.
//
// Power (UpdatePowerInRect, message 0x8A07AE00). Each lot of the rectangle
// (lot manager slot 7): powered when the development simulator's slot 55
// (a cheat) says so or the power simulator (slot 37) has power at the
// building's centre cell, or the lot's own cell without a building. A
// change is written to the lot (slot 67) and moves its condition 0 -> 1
// (lost) or 1 -> 0 (back). An unpowered lot that needs power (always
// without a building) gets the zot 0x0FD10000 of kind 4, without a
// building only on the cells 0x006C3B10 accepts; any other lot loses a
// zot of kind 4.
//
// Day and night (the buildings' lights). Message 0x44778234 makes DoMessage
// either finish at once (FinishNightBuildings: 0x006C8E10 for every
// building of the deque +0x64) or start stepping (+0x9C = 1 or 2);
// StepNightBuildings then takes +0xA0 buildings every 200 ms of the timer
// message until the deque's end.
//
// Flora of a lot (PlantLotFlora, when a lot goes from state 2 to 3): each
// configuration object of kind 4 gets a flora occupant at its position
// (slot 39) of a type drawn from its candidates (ChooseFloraType: one
// draw from the static generator 0x00B49AF0; a candidate that is a family
// gives one of its types by a second draw).
//
// The lot update (StepLotUpdate, message 0x66956814). State +0xAC: 0 or 3
// starts a round (the lot manager's iterator +0xB0 fetched or rewound;
// +0x5D and +0x5E moved to +0xB8 and +0xB9 and cleared; +0xBA = the date
// is past +0x60), 1 runs it: lots / 28 + 1 lots a call within a time
// budget of 5 (the shared deadline code, step global 0x00B49B08); at the
// list's end the state is 3 and the byte 0x00B49AE9 cleared. Returns
// false while a call's share is unfinished. UpdateLot, per lot:
//   - with +0xB8: road access refreshed; a lot of zone type 7-9 with a
//     building, no travel designate and no road access goes to 0x006C4910
//     (while 0x00B49AE9 is set);
//   - with a building whose profile starts with 0: the check 0x006CABB0
//     (2 = fine) moves the condition (0 -> 1 when not fine, 1 -> 0 when
//     fine, 5 and 6 -> 0) and, when fine, 0x006CAFC0 runs;
//   - with any other building: four checks (0x006C47C0, 0x006CABB0,
//     0x006CAE30, 0x006CAFC0); the last that fails gives the reason 1-4
//     and state 4, written when the state changes (result true); the
//     condition from 0x006C4630; 0x006C78D0 for profiles starting with 2
//     under the development simulator's slot 58;
//   - without a building, with +0xBA: zones 1-6 unchanged for more than
//     30 days with demand (+0x5F) run 0x006CAFC0 on a blank profile of
//     purpose 1 (zones 1-3) or 3 (4-6); on the cells 0x006C3B10 accepts,
//     0x006CABB0;
//   - the lot's business unit: when the lot is the unit's first member,
//     the travel designates (slot 34, with +0xB9) and the travel
//     populations (slot 35); without a unit the lot's travel population
//     is reset to itself and a livable lot without road access gets a
//     unit (0x006CB260).
//
// Travel designates (UpdateTravelDesignates). The first member of the unit
// with road access and a building becomes the designate of every member
// whose own designate is missing, not in the unit or without road access.
// Without such a member the unit is taken apart: each member is removed
// and looked at again (0x006CB260) until one has a new unit, which then
// takes every member; that unit (or null) is written back to the caller,
// unreferenced, as the game does.
//
// Return values. The header (gzcom-dll) declares most of these bool, but
// RemoveBuildingZotProp, StartDemolishLot and ExplodeLot leave in EAX what
// their last call returned; ours return the same call's value.
// UpdateTravelDesignates leaves the result of a Release or of the free
// function; ours returns 1.
//
// Fields used (offsets from the object's start; evidence: Init 0x006CB9A0
// for the services, the functions here for the rest):
//   0x18  the building development simulator (0x00B43D68)
//   0x1C  the city (0x00B43CEC): slot 96 (x, z, float*, float*) a cell's
//         corner position, slots 91 and 92 the cell's size (float)
//   0x24  the lot manager (0x00B43D08): slot 6 (x, z, 0) the lot at a
//         cell, 7 (list*, x0, z0, x1, z1, 1) the lots of a rectangle, 15
//         its lot iterator, 16 the lot count
//   0x28  the occupant manager (0x00B43D0C)
//   0x30  the prop manager (0x00B43D10)
//   0x34  the power simulator (0x00B43DA0)
//   0x38  the flora simulator (0x00B43D80): slot 11 (type) valid, 14
//         (hash set*, family), 17 (type, position, 0) a new flora
//         occupant, 20 (rect*, 0) clear
//   0x4C  int32: the city's cells along z (city slot 94); 0x50 along x
//   0x5D, 0x5E  uint8: the update's two "dirty" flags, taken at a round's
//         start
//   0x5F  uint8: demand flag (message 0x69247DC7)
//   0x60  uint32: a date (message 0x26D31EC4)
//   0x64  std::deque<building*>: begin iterator {cur, first, last, node};
//         0x74 its end iterator; blocks of 32 pointers
//   0x8C  deque iterator: the next building of the night step
//   0x9C  uint32: 0 idle, 1 or 2 stepping (2 = night); 0xA0 uint32:
//         buildings per step; 0xA4 uint32: milliseconds gathered
//   0xAC  uint32: the lot update's state; 0xB0 the lot iterator {current
//         node, list*}; 0xB4 int32: lots done in this share
//   0xB8, 0xB9, 0xBA  uint8: the round's copies of +0x5D, +0x5E and
//         "the date is past +0x60"
//   0xD4  float: the slope above which things get a foundation
//   0xE0, 0xE1  uint8 tuning: level the area to the limit; level the
//         inner vertices
//   0xE8  float (passed on as its bits): AttemptAreaLevelling's limit
#include "core/lot_sim.h"

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
#include <cstring>
#include <iterator>

namespace opensc4::lot_sim {
namespace {

namespace local {
constexpr std::size_t kDevelopment = 0x18;
constexpr std::size_t kCity = 0x1C;
constexpr std::size_t kLots = 0x24;
constexpr std::size_t kOccupants = 0x28;
constexpr std::size_t kProps = 0x30;
constexpr std::size_t kPower = 0x34;
constexpr std::size_t kFlora = 0x38;
constexpr std::size_t kCellsZ = 0x4C;
constexpr std::size_t kCellsX = 0x50;
constexpr std::size_t kDirty5D = 0x5D;
constexpr std::size_t kDirty5E = 0x5E;
constexpr std::size_t kDemand = 0x5F;
constexpr std::size_t kDate = 0x60;
constexpr std::size_t kBuildingsBegin = 0x64;
constexpr std::size_t kBuildingsEnd = 0x74;
constexpr std::size_t kNextBuilding = 0x8C;
constexpr std::size_t kNightState = 0x9C;
constexpr std::size_t kStepCount = 0xA0;
constexpr std::size_t kStepMs = 0xA4;
constexpr std::size_t kUpdateState = 0xAC;
constexpr std::size_t kLotIterator = 0xB0;
constexpr std::size_t kUpdateCount = 0xB4;
constexpr std::size_t kUpdateB8 = 0xB8;
constexpr std::size_t kUpdateB9 = 0xB9;
constexpr std::size_t kUpdateBA = 0xBA;
constexpr std::size_t kFoundationSlope = 0xD4;
constexpr std::size_t kLevelArea = 0xE0;
constexpr std::size_t kLevelInner = 0xE1;
constexpr std::size_t kLevelLimit = 0xE8;
} // namespace local

// The class's primary vtable (the object part's entry in lot.toml).
constexpr std::uintptr_t kVtablePrimary = 0x00AB1B98;

// Functions of the other parts, called at the game's addresses (their
// entries are in data/addresses/lot.toml).
constexpr std::uintptr_t kCellShowsZot = 0x006C3B10;   // object part; lot in ECX, z in EAX, x in EDX
constexpr std::uintptr_t kSetLotState = 0x006C3C50;    // object part: (lot, state, a, b)
constexpr std::uintptr_t kFn_006C42D0 = 0x006C42D0;    // object part: (lot)
constexpr std::uintptr_t kFn_006C4630 = 0x006C4630;    // object part: (lot, x, z) -> condition
constexpr std::uintptr_t kFn_006C47C0 = 0x006C47C0;    // object part: (lot, x, z) -> bool
constexpr std::uintptr_t kFn_006C4910 = 0x006C4910;    // object part: (lot)
constexpr std::uintptr_t kFn_006C4BD0 = 0x006C4BD0;    // object part: (lot, corner, corner, burnt)
constexpr std::uintptr_t kFn_006C78D0 = 0x006C78D0;    // build part: (lot)
constexpr std::uintptr_t kNightBuilding = 0x006C8E10;  // build part: (building, night)
constexpr std::uintptr_t kFn_006C91F0 = 0x006C91F0;    // build part: (lot)
constexpr std::uintptr_t kRemoveZot = 0x006C9780;      // build part: (lot, kind)
constexpr std::uintptr_t kAddZot = 0x006C98C0;         // build part: (lot, zot, kind)
constexpr std::uintptr_t kFn_006C9AD0 = 0x006C9AD0;    // build part: (lot, configuration, type, foundation)
constexpr std::uintptr_t kFn_006C9FB0 = 0x006C9FB0;    // build part: (lot)
constexpr std::uintptr_t kFn_006CAAF0 = 0x006CAAF0;    // build part: (lot)
constexpr std::uintptr_t kFn_006CABB0 = 0x006CABB0;    // build part: (lot, x, z) -> uint32
constexpr std::uintptr_t kFn_006CAE30 = 0x006CAE30;    // build part: (lot, x, z) -> bool
constexpr std::uintptr_t kFn_006CAFC0 = 0x006CAFC0;    // build part: (lot, profile, x, z) -> bool
constexpr std::uintptr_t kUpdateUnit = 0x006CB260;     // build part: (lot)

// This part's own functions (data/addresses/lot.toml).
constexpr std::uintptr_t kStepNightBuildings = 0x006CBD60;
constexpr std::uintptr_t kFinishNightBuildings = 0x006CBE50;
constexpr std::uintptr_t kUpdatePowerInRect = 0x006CBF40;
constexpr std::uintptr_t kPlantLotFlora = 0x006CD2B0;
constexpr std::uintptr_t kStepLotUpdate = 0x006CD770;

// Game code and data outside the class.
constexpr std::uintptr_t kEffectAllocate = 0x005EDAE0;        // cdecl (0xD8)
constexpr std::uintptr_t kEffectConstruct = 0x005EF810;       // thiscall (1)
constexpr std::uintptr_t kEffectSetOccupant = 0x005EEB70;     // thiscall (occupant)
constexpr std::uintptr_t kCraterAllocate = 0x005ED6E0;        // cdecl (0xA0)
constexpr std::uintptr_t kCraterConstruct = 0x005EE360;       // thiscall ()
constexpr std::uintptr_t kConstructionAllocate = 0x005CA8A0;  // cdecl (0x1A0)
constexpr std::uintptr_t kConstructionConstruct = 0x005D00F0; // thiscall (1)
constexpr std::uintptr_t kProfileConstruct = 0x004591E0;      // thiscall ()
constexpr std::uintptr_t kProfileDestroy = 0x00459220;        // thiscall ()
constexpr std::uintptr_t kHashNextBucket = 0x0053DCF0;        // thiscall on the iterator
constexpr std::uintptr_t kRandom = 0x00B49AF0;                // the class's static cRZRandom
constexpr std::uintptr_t kFloraOrigin = 0x00B49AEC;           // a float nothing writes
constexpr std::uintptr_t kUpdateFlag = 0x00B49AE9;            // uint8
constexpr std::uintptr_t kUpdateStep = 0x00B49B08;            // the update's deadline step; counter at +4
constexpr std::uintptr_t kUpdateStepOnce = 0x00B49B10;        // bit 0: the step is set up

constexpr uint32_t kZotProperty = 0xC999C45E;

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(address)(self, args...);
}

template <typename R, typename... Args>
R CallC(std::uintptr_t address, Args... args)
{
    using Fn = R(__cdecl*)(Args...);
    return reinterpret_cast<Fn>(address)(args...);
}

void* Allocate(std::size_t size) { return CallC<void*>(addr::SC4_stl_allocate, size); }
void Free(void* block) { CallC<void>(addr::SC4_Free, block); }
void AddRef(void* object) { Virtual<uint32_t>(object, 0x4); }
void Release(void* object) { Virtual<uint32_t>(object, 0x8); }

// __ftol (0x009EEF04): truncation.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The low byte of a stack slot replaced, as the game does when it keeps a
// bool in the slot of an argument it no longer needs and later pushes the
// whole slot.
uint32_t WithLowByte(uint32_t slot, uint8_t value) { return (slot & ~0xFFu) | value; }

uint32_t Address(void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A new empty list of the STL allocator (the sentinel node).
ListNode* NewList()
{
    auto* const list = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
    list->next = list;
    list->previous = list;
    return list;
}

// Releases the values, frees the nodes and the sentinel.
void DestroyReferencedList(ListNode* list)
{
    for (ListNode* node = list->next; node != list;) {
        ListNode* const dead = node;
        void* const value = dead->value;
        node = node->next;
        if (value)
            Release(value);
        Free(dead);
    }
    list->next = list;
    list->previous = list;
    Free(list);
}

struct DequeIterator {
    void** current;
    void** first;
    void** last;
    void*** node;
};

// ++ of the building deque's iterator (blocks of 0x80 bytes).
void Advance(DequeIterator& iterator)
{
    ++iterator.current;
    if (iterator.current == iterator.last) {
        ++iterator.node;
        iterator.first = *iterator.node;
        iterator.last = iterator.first + 0x20;
        iterator.current = iterator.first;
    }
}

struct HashNode {
    HashNode* next;
    uint32_t value;
};

struct HashSet {
    uint32_t unused;
    HashNode** buckets;
    HashNode** bucketsEnd;
    HashNode** bucketsCapacity;
    uint32_t count;
};

struct HashIterator {
    HashNode* node;
    HashSet* table;
};

void ConstructSet(HashSet& set)
{
    set.unused = 0; // the game leaves this one as the stack had it
    set.buckets = nullptr;
    set.bucketsEnd = nullptr;
    set.bucketsCapacity = nullptr;
    set.count = 0;
    CallThis<void>(addr::SC4Uint32HashSet_Construct, &set, 100u);
}

void DestroySet(HashSet& set)
{
    CallThis<void>(addr::SC4Uint32HashSet_Clear, &set);
    if (set.buckets)
        Free(set.buckets);
}

// 0x006C3B10: whether the cell is one where a lot of this zone type shows
// its zot. The game passes the lot in ECX, z in EAX and x in EDX.
void* g_cellShowsZot = reinterpret_cast<void*>(kCellShowsZot);

__declspec(naked) uint8_t __fastcall CellShowsZot(void* /*lot*/, int32_t /*x*/, int32_t /*z*/)
{
    __asm mov eax, [esp + 4]
    __asm call dword ptr [g_cellShowsZot]
    __asm ret 4
}

// The lot's zot property (0xC999C45E) as a variant, or null.
void* ZotValue(void* lot)
{
    void* const holder = Virtual<void*>(lot, 0xC);
    void* const property = Virtual<void*>(holder, 0x24, kZotProperty);
    return property ? Virtual<void*>(property, 0x14) : nullptr;
}

// --- Travel designates -----------------------------------------------------------

// 0x006CB4D0, slot 34.
uint8_t __fastcall UpdateTravelDesignates(void* self, void*, void** unit)
{
    static bool logged = false;
    FirstCall("UpdateTravelDesignates", logged);
    // A vector that never holds more than the first lot with road access
    // and a building.
    void** begin = nullptr;
    void** end = nullptr;
    ListNode** const members = Virtual<ListNode**>(*unit, 0x14);
    for (ListNode* node = (*members)->next; node != *members; node = node->next) {
        void* const lot = node->value;
        if (!Virtual<uint8_t>(lot, 0x70) || !Virtual<void*>(lot, 0xC0))
            continue;
        AddRef(lot);
        begin = static_cast<void**>(Allocate(sizeof(void*)));
        if (begin) {
            *begin = lot;
            AddRef(lot);
        }
        end = begin + 1;
        Release(lot);
        break;
    }
    if (end - begin > 0) {
        for (ListNode* node = (*members)->next; node != *members; node = node->next) {
            void* const lot = node->value;
            void* const designate = Virtual<void*>(lot, 0xEC);
            bool outside = false;
            if (designate) {
                ListNode* const head = *members;
                AddRef(designate);
                ListNode* at = (*members)->next;
                while (at != *members && at->value != designate)
                    at = at->next;
                outside = at == head;
                Release(designate);
            }
            if (designate && !outside && Virtual<uint8_t>(designate, 0x70))
                continue;
            if (designate)
                Virtual<void>(lot, 0xF0, static_cast<void*>(nullptr));
            if (lot != *begin)
                Virtual<void>(lot, 0xF0, *begin);
        }
    } else {
        ListNode** const again = Virtual<ListNode**>(*unit, 0x14);
        void* found = nullptr;
        // A copy of the member list, each lot referenced (0x006C6CA0, the
        // list's range insert, written out).
        ListNode* const copy = NewList();
        {
            ListNode* const head = *again;
            for (ListNode* node = head->next; node != head; node = node->next) {
                auto* const fresh = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
                fresh->value = node->value;
                if (fresh->value)
                    AddRef(fresh->value);
                ListNode* const last = copy->previous;
                fresh->next = copy;
                fresh->previous = last;
                last->next = fresh;
                copy->previous = fresh;
            }
        }
        for (ListNode* node = copy->next; node != copy; node = node->next) {
            void* const lot = node->value;
            Virtual<void>(*unit, 0x10, lot);
            CallThis<void>(kUpdateUnit, self, lot);
            found = Virtual<void*>(lot, 0x64);
            if (!found)
                continue;
            for (ListNode* member = copy->next; member != copy; member = member->next) {
                void* const other = member->value;
                if (void* const old = Virtual<void*>(other, 0x64))
                    Virtual<void>(old, 0x10, other);
                Virtual<void>(found, 0xC, other);
            }
            break;
        }
        *unit = found; // no reference taken or given up, as the game
        DestroyReferencedList(copy);
    }
    for (void** at = begin; at != end; ++at)
        if (*at)
            Release(*at);
    if (begin)
        Free(begin);
    return 1;
}

// --- A configuration object's occupant -------------------------------------------

// 0x006CB760, slot 64.
uint8_t __fastcall GetLotConfigurationObjectOccupant(void* self, void*, void* lot, uint32_t index, void** occupant)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationObjectOccupant", logged);
    *occupant = nullptr;
    if (!lot)
        return 0;
    auto* list = static_cast<ListNode*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    list->next = list;
    list->previous = list;
    int32_t rect[4] = {};
    Virtual<void>(lot, 0x38, rect);
    void* const city = At<void*>(self, local::kCity);
    float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
    Virtual<void>(city, 0x180, rect[0], rect[1], &x0, &z0);
    Virtual<void>(city, 0x180, rect[2] + 1, rect[3] + 1, &x1, &z1);
    uint8_t found = 0;
    if (void* const configuration = Virtual<void*>(lot, 0x24)) {
        char** const objects = Virtual<char**>(configuration, 0x5C);
        const uint32_t wanted = At<uint32_t>(*objects, index * 0x34 + 0x24);
        if (Virtual<uint8_t>(At<void*>(self, local::kOccupants), 0xA0, &list, &rect[0], &rect[2], 0u, 0xFFFFFFFFu)) {
            uint32_t id = index; // the game reads the ID into the index argument's slot
            ListNode* const head = list;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const candidate = node->value;
                float position[3] = {};
                Virtual<void>(candidate, 0x20, position);
                if (position[0] >= x0 && position[2] >= z0 && position[0] <= x1 && position[2] <= z1 &&
                    Virtual<uint8_t>(candidate, 0x6C, &id) && id == wanted) {
                    if (*occupant)
                        Release(*occupant);
                    *occupant = candidate;
                    AddRef(candidate);
                }
                Release(candidate);
            }
            if (*occupant)
                found = 1;
        }
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
    return found;
}

// --- Zots ------------------------------------------------------------------------

// 0x006CB940, slot 11.
uint32_t __fastcall RemoveBuildingZotProp(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("RemoveBuildingZotProp", logged);
    void* const value = ZotValue(lot);
    if (!value)
        return 0;
    const uint32_t kind = *Virtual<uint32_t*>(value, 0x124);
    if (kind)
        CallThis<void>(kRemoveZot, self, lot, kind);
    return Virtual<uint32_t>(value, 0x68, 0u);
}

// --- Day and night -----------------------------------------------------------------

// 0x006CBD60. `milliseconds` keeps its upper bytes in what 0x006C8E10 is
// given: the game stores the night flag in the argument's low byte.
void __fastcall StepNightBuildings(void* self, void*, uint32_t milliseconds)
{
    static bool logged = false;
    FirstCall("StepNightBuildings", logged);
    uint32_t& gathered = At<uint32_t>(self, local::kStepMs);
    gathered += milliseconds;
    if (gathered <= 200)
        return;
    const uint32_t night = WithLowByte(milliseconds, At<uint32_t>(self, local::kNightState) == 2 ? 1 : 0);
    uint32_t left = At<uint32_t>(self, local::kStepCount);
    gathered = 0;
    if (!left)
        return;
    DequeIterator& next = At<DequeIterator>(self, local::kNextBuilding);
    do {
        --left;
        if (next.current == At<DequeIterator>(self, local::kBuildingsEnd).current) {
            At<uint32_t>(self, local::kNightState) = 0;
            return;
        }
        void* const building = *next.current;
        Advance(next);
        CallThis<void>(kNightBuilding, self, building, night);
    } while (left);
}

// 0x006CBE50.
void __fastcall FinishNightBuildings(void* self, void*, uint32_t state)
{
    static bool logged = false;
    FirstCall("FinishNightBuildings", logged);
    const uint32_t night = WithLowByte(state, state == 2 ? 1 : 0);
    DequeIterator& next = At<DequeIterator>(self, local::kNextBuilding);
    next = At<DequeIterator>(self, local::kBuildingsBegin);
    while (next.current != At<DequeIterator>(self, local::kBuildingsEnd).current) {
        CallThis<void>(kNightBuilding, self, *next.current, night);
        Advance(next);
    }
    next = At<DequeIterator>(self, local::kBuildingsBegin);
    At<uint32_t>(self, local::kNightState) = 0;
}

// --- Power -------------------------------------------------------------------------

// 0x006CBF40. The argument is the rectangle's address; the game keeps the
// lot's power flag in that slot's low byte and passes the slot to the lot.
void __fastcall UpdatePowerInRect(void* self, void*, uint32_t rectSlot)
{
    static bool logged = false;
    FirstCall("UpdatePowerInRect", logged);
    const int32_t* const rect = reinterpret_cast<const int32_t*>(static_cast<std::uintptr_t>(rectSlot));
    ListNode* list = NewList();
    if (Virtual<uint32_t>(At<void*>(self, local::kLots), 0x1C, &list, rect[0], rect[1], rect[2], rect[3], 1u)) {
        for (ListNode* node = list->next; node != list; node = node->next) {
            void* const lot = node->value;
            void* const building = Virtual<void*>(lot, 0xC0);
            uint8_t powered = 0;
            int32_t lotRect[4] = {};
            Virtual<void>(lot, 0x38, lotRect);
            int32_t x = 0, z = 0;
            if (building) {
                void* const occupant = Virtual<void*>(building, 0xC);
                float a[3] = {1.0f, 1.0f, 1.0f};
                float b[3] = {0.0f, 0.0f, 0.0f};
                Virtual<void>(occupant, 0x28, a, b);
                void* const city = At<void*>(self, local::kCity);
                float sum = static_cast<float>(static_cast<double>(b[0]) + static_cast<double>(a[0]));
                double cell = Virtual<double>(city, 0x16C);
                x = Ftol(static_cast<double>(sum) / (cell + cell));
                sum = static_cast<float>(static_cast<double>(b[2]) + static_cast<double>(a[2]));
                cell = Virtual<double>(city, 0x170);
                z = Ftol(static_cast<double>(sum) / (cell + cell));
            } else {
                Virtual<void>(lot, 0x54, &x, &z);
            }
            if (Virtual<uint8_t>(At<void*>(self, local::kDevelopment), 0xDC) ||
                Virtual<uint8_t>(At<void*>(self, local::kPower), 0x94, x, z))
                powered = 1;
            if (powered != Virtual<uint8_t>(lot, 0x108)) {
                Virtual<void>(lot, 0x10C, WithLowByte(rectSlot, powered));
                const uint32_t condition = Virtual<uint32_t>(lot, 0x8C);
                if (!powered) {
                    if (condition == 0)
                        Virtual<void>(lot, 0x90, 1u);
                } else if (condition == 1) {
                    Virtual<void>(lot, 0x90, 0u);
                }
            }
            uint8_t needs = 1;
            if (building && !Virtual<uint8_t>(lot, 0x104))
                needs = 0;
            if (!powered && needs) {
                if (building || CellShowsZot(lot, x, z))
                    CallThis<void>(kAddZot, self, lot, 0x0FD10000u, 4u);
            } else if (void* const value = ZotValue(lot)) {
                if (*Virtual<uint32_t*>(value, 0x124) == 4) {
                    CallThis<void>(kRemoveZot, self, lot, 4u);
                    Virtual<void>(value, 0x68, 0u);
                }
            }
        }
    }
    DestroyReferencedList(list);
}

// --- Demolition ----------------------------------------------------------------------

// 0x006CC1D0, slot 12.
uint32_t __fastcall StartDemolishLot(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("StartDemolishLot", logged);
    if (Virtual<uint32_t>(lot, 0x30) & 1) {
        int32_t rect[4] = {};
        Virtual<void>(lot, 0x38, rect);
        Virtual<void>(At<void*>(self, local::kFlora), 0x50, rect, 0u);
    }
    Virtual<void>(At<void*>(self, local::kProps), 0x70, lot);
    if (void* const value = ZotValue(lot))
        Virtual<void>(value, 0x68, 0u);
    CallThis<void>(kFn_006C9FB0, self, lot);
    return CallThis<uint32_t>(kSetLotState, self, lot, 5u, 1u, 0u);
}

// 0x006CC250, slot 14. `burnt` is passed on as the whole argument slot.
uint32_t __fastcall ExplodeLot(void* self, void*, void* lot, uint32_t burnt)
{
    static bool logged = false;
    FirstCall("ExplodeLot", logged);
    uint32_t state = Virtual<uint32_t>(lot, 0x84);
    if (state == 5)
        return state;
    state = Virtual<uint32_t>(lot, 0x84);
    if (state == 6)
        return state;
    float size = 0.0f;
    float position[3] = {};
    float p[3] = {};
    float q[3] = {};
    void* effect = nullptr;
    bool hasBuilding = false;
    if (void* const building = Virtual<void*>(self, 0x98, lot)) {
        Virtual<void>(Virtual<void*>(building, 0xC), 0x20, position);
        Virtual<void>(Virtual<void*>(building, 0xC), 0x28, p, q);
        // The larger of the box's two sides, doubled. The game compares the
        // x side while it is still on the FPU stack with the stored z side.
        const float dz = static_cast<float>(static_cast<double>(q[2]) - static_cast<double>(p[2]));
        const double dxExact = static_cast<double>(q[0]) - static_cast<double>(p[0]);
        const float dx = static_cast<float>(dxExact);
        const float side = dxExact < static_cast<double>(dz) ? dz : dx;
        size = static_cast<float>(static_cast<double>(side) + static_cast<double>(side));
        void* const block = CallC<void*>(kEffectAllocate, 0xD8u);
        effect = block ? CallThis<void*>(kEffectConstruct, block, 1) : nullptr;
        AddRef(effect);
        void* const occupant = Virtual<void*>(building, 0xC);
        CallThis<void>(kEffectSetOccupant, effect, occupant);
        void* const part = static_cast<char*>(effect) + 0x8;
        Virtual<void>(part, 0x74, 0x40u);
        Virtual<void>(part, 0xC);
        hasBuilding = true;
    }
    CallThis<void>(kSetLotState, self, lot, 6u, 0u, 4u);
    CallThis<void>(kFn_006C42D0, self, lot);
    CallThis<void>(kFn_006C9FB0, self, lot);
    void* const props = Global<void*>(addr::SC4_Service43D10);
    if (hasBuilding) {
        uint32_t ids[3] = {0x7AB50E44, 0x1ABE787D, static_cast<uint8_t>(burnt) ? 0x49C80EC7u : 0x2A2EF55Du};
        void* const block = CallC<void*>(kCraterAllocate, 0xA0u);
        void* crater = nullptr;
        if (block) {
            crater = CallThis<void*>(kCraterConstruct, block);
            if (crater)
                AddRef(crater);
        }
        void* const part = static_cast<char*>(crater) + 0xC;
        Virtual<void>(part, 0xC);
        Virtual<void>(crater, 0xC, ids, Bits(size));
        Virtual<void>(crater, 0x10, 0x3F333333u); // 0.7
        Virtual<void>(part, 0x24, position);
        Virtual<void>(part, 0x74, 0x40u);
        Virtual<void>(Global<void*>(addr::SC4_Service43D10), 0x64, lot, part);
        CallThis<void>(kFn_006C4BD0, self, lot, p, q, burnt);
        Release(crater);
    }
    if (effect) {
        Virtual<void>(props, 0x64, lot, static_cast<void*>(static_cast<char*>(effect) + 0x8));
        Release(effect);
    }
    if (static_cast<uint8_t>(burnt))
        Virtual<void>(lot, 0x90, 6u);
    return Virtual<uint32_t>(lot, 0xBC, 0u);
}

// --- Construction ------------------------------------------------------------------

// 0x006CC490, slot 6.
uint8_t __fastcall StartConstruction(void* self, void*, void* lot, void* configuration, uint32_t buildingType,
                                     uint32_t keepState)
{
    static bool logged = false;
    FirstCall("StartConstruction", logged);
    uint8_t result = 0;
    AddRef(lot);
    Virtual<void>(lot, 0x28, configuration);
    Virtual<void>(lot, 0xBC, buildingType);
    Virtual<void>(lot, 0x88, 2u, 0u, 0u);
    Virtual<void>(lot, 0x90, 1u);
    CallThis<void>(kUpdateUnit, self, lot);
    const uint32_t flags = Virtual<uint32_t>(lot, 0x30);
    Virtual<void>(lot, 0x34, flags & ~7u);
    Virtual<void>(lot, 0x114, 1u);
    if (void* const value = ZotValue(lot)) {
        const uint32_t kind = *Virtual<uint32_t*>(value, 0x124);
        CallC<void>(addr::SC4_PostMessage, 0x4C04DE69u, 2u, 0u, 0u, 0u, Address(lot));
        if (kind == 6) {
            CallThis<void>(kRemoveZot, self, lot, 6u);
            Virtual<void>(value, 0x68, 0u);
        }
    }
    bool built = true;
    if (buildingType) {
        void* const development = At<void*>(self, local::kDevelopment);
        const uint32_t* const profile = Virtual<const uint32_t*>(development, 0x9C, buildingType);
        const bool level = *profile != 5;
        const uint8_t flag = Virtual<uint8_t>(configuration, 0x38);
        // The flag lives in the low byte of the lot argument's slot, and
        // the slot is what the slope and levelling slots are given.
        const uint32_t flagSlot = WithLowByte(Address(lot), flag);
        uint8_t foundation = 0;
        if (flag && Virtual<double>(configuration, 0x28) == 0.0)
            foundation = Virtual<uint8_t>(self, 0xD8, lot, configuration, 0u, 1u, 1u);
        if (!foundation) {
            int32_t rect[4] = {};
            if (level && At<uint8_t>(self, local::kLevelInner)) {
                Virtual<void>(lot, 0x38, rect);
                if (rect[2] - rect[0] > 1 && rect[3] - rect[1] > 1)
                    Virtual<void>(self, 0x4C, rect, 0xBF800000u, flagSlot, 0u, lot); // -1.0
            }
            // The first comparison is made on the FPU stack, before the
            // slope is rounded to a float.
            const double slopeExact = Virtual<double>(self, 0x78, lot, flagSlot);
            float slope = static_cast<float>(slopeExact);
            if (slopeExact > static_cast<double>(At<float>(self, local::kFoundationSlope))) {
                const float limit = static_cast<float>(Virtual<double>(configuration, 0x28));
                if (slope > limit) {
                    if (level && At<uint8_t>(self, local::kLevelArea)) {
                        Virtual<void>(lot, 0x38, rect);
                        if (Virtual<uint32_t>(self, 0x54, rect, Bits(limit), 1u, flagSlot,
                                              At<uint32_t>(self, local::kLevelLimit), lot))
                            slope = static_cast<float>(Virtual<double>(self, 0x78, lot, flagSlot));
                    }
                    if (slope > limit)
                        foundation = Virtual<uint8_t>(self, 0xD8, lot, configuration, 0u, 1u, 1u);
                }
            }
        }
        // The upper bytes of this argument are stack the game never wrote;
        // zeros here.
        CallThis<void>(kFn_006C9AD0, self, lot, configuration, buildingType, static_cast<uint32_t>(foundation));
        uint32_t orientation = 0;
        float position[3] = {}; // not set by the game before the call
        Virtual<void>(self, 0xA0, lot, position, &orientation);
        void* const building = Virtual<void*>(development, 0x1C, buildingType, position, orientation);
        if (building) {
            if (!foundation) {
                void* occupant = Virtual<void*>(building, 0xC);
                const double ownExact = Virtual<double>(self, 0x7C, occupant, flagSlot);
                const float own = static_cast<float>(ownExact);
                if (ownExact > static_cast<double>(At<float>(self, local::kFoundationSlope))) {
                    int32_t cells[4] = {};
                    occupant = Virtual<void*>(building, 0xC);
                    Virtual<void>(self, 0x58, occupant, cells);
                    bool steep;
                    if (Virtual<uint32_t>(self, 0x54, cells, At<uint32_t>(self, local::kFoundationSlope), 1u, flagSlot,
                                          At<uint32_t>(self, local::kLevelLimit), lot)) {
                        occupant = Virtual<void*>(building, 0xC);
                        steep = Virtual<double>(self, 0x7C, occupant, flagSlot) >
                                static_cast<double>(At<float>(self, local::kFoundationSlope));
                    } else {
                        steep = own > At<float>(self, local::kFoundationSlope);
                    }
                    if (steep)
                        Virtual<void>(self, 0xE8, building, configuration);
                }
            }
            if (static_cast<uint8_t>(keepState)) {
                void* construction = nullptr;
                void* const com = CallC<void*>(addr::RZGetCOM);
                if (!Virtual<uint8_t>(com, 0xC, 0xA97F909Eu, 0x496E7636u, &construction)) {
                    void* const block = CallC<void*>(kConstructionAllocate, 0x1A0u);
                    construction = block ? CallThis<void*>(kConstructionConstruct, block, 1) : nullptr;
                    AddRef(construction);
                }
                Virtual<void>(construction, 0x10, lot);
                void* const occupant = Virtual<void*>(construction, 0xC);
                Virtual<void>(At<void*>(self, local::kOccupants), 0x50, occupant, 0u);
                Release(construction);
            }
        } else {
            built = false;
        }
    }
    if (built)
        result = 1;
    if (!static_cast<uint8_t>(keepState))
        Virtual<void>(lot, 0x88, 3u, 0u, 0u);
    Release(lot);
    return result;
}

// 0x006CCEF0, slot 7.
uint8_t __fastcall ConstructDefaultLot(void* self, void*, void* lot, void* configuration, uint32_t buildingType,
                                       uint32_t keepState)
{
    static bool logged = false;
    FirstCall("ConstructDefaultLot", logged);
    if (configuration && !buildingType) {
        char* const object = Virtual<char*>(configuration, 0x60);
        if (object && At<uint32_t*>(object, 0x28) != At<uint32_t*>(object, 0x2C)) {
            void* const development = At<void*>(self, local::kDevelopment);
            HashSet set;
            ConstructSet(set);
            const int32_t count = static_cast<int32_t>(At<uint32_t*>(object, 0x2C) - At<uint32_t*>(object, 0x28));
            for (int32_t i = 0; i < count; ++i) {
                uint32_t type = At<uint32_t*>(object, 0x28)[i];
                if (Virtual<uint8_t>(development, 0x3C, type)) {
                    char inserted[16]; // the callee writes a 9-byte pair
                    CallThis<void>(addr::SC4Uint32HashSet_InsertUnique, &set, static_cast<void*>(inserted), &type);
                } else {
                    Virtual<void>(development, 0x4C, &set, type);
                }
            }
            if (set.count) {
                HashIterator first{};
                CallThis<void>(addr::SC4HashTable_Begin, &set, &first);
                buildingType = first.node->value;
            }
            DestroySet(set);
        }
    }
    return Virtual<uint8_t>(self, 0x18, lot, configuration, buildingType, keepState);
}

// --- Lot elements ----------------------------------------------------------------------

// 0x006CC880, slot 37.
uint8_t __fastcall UpdateLotElements(void* self, void*, void* lot, uint32_t reason)
{
    static bool logged = false;
    FirstCall("UpdateLotElements", logged);
    if (Virtual<int32_t>(lot, 0x84) < 2)
        return 1;
    uint8_t changed = 0;
    if (Virtual<uint32_t>(lot, 0x30) & 3) {
        Virtual<void>(self, 0xF4, lot);
        Virtual<void>(self, 0xF8, lot);
    } else if (reason != 5) {
        if (void* const configuration = Virtual<void*>(lot, 0x24)) {
            const uint32_t flagSlot = WithLowByte(Address(lot), Virtual<uint8_t>(configuration, 0x38));
            const float slope = static_cast<float>(Virtual<double>(self, 0x78, lot, flagSlot));
            if (Virtual<double>(configuration, 0x28) < static_cast<double>(slope)) {
                const uint32_t kindSlot = WithLowByte(reason, Virtual<uint8_t>(configuration, 0x6C, 7u));
                Virtual<void>(self, 0xD8, lot, configuration, kindSlot, 0u, 1u);
                changed = 1;
            }
            if (std::fabs(slope) > 0.25f)
                CallThis<void>(kFn_006C91F0, self, lot);
        }
    }
    // The flag's slot is the ECX the function pushed on entry: `this`.
    Virtual<void>(self, 0xB4, lot, WithLowByte(Address(self), changed));
    Virtual<void>(lot, 0x50);
    return 1;
}

// --- The empty lot's base texture ---------------------------------------------------------

struct TextureCell {
    uint32_t texture;
    uint8_t x;
    uint8_t z;
    uint8_t orientation;
    uint8_t flag;
    uint32_t color;
    uint8_t byteC;
    uint8_t byteD;
    uint8_t unset[2]; // stack the game never writes; zeros here
};
static_assert(sizeof(TextureCell) == 0x10);

// 0x006CC970, slot 43.
void __fastcall SetEmptyLotBaseTexture(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("SetEmptyLotBaseTexture", logged);
    const uint32_t zone = Virtual<uint32_t>(lot, 0x7C);
    if (zone <= 0xF && ((0x9801u >> zone) & 1)) { // 0, 11, 12, 15
        CallThis<void>(kFn_006CAAF0, self, lot);
        return;
    }
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    const uint32_t drag = Virtual<uint32_t>(zones, 0x3C, zone);
    const uint8_t alpha = Virtual<uint8_t>(zones, 0x40);
    const uint32_t color = (drag >> 24) | (((drag >> 16) & 0xFF) << 8) | (((drag >> 8) & 0xFF) << 16) |
                           (static_cast<uint32_t>(alpha) << 24);
    const int32_t maxZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178) - 1;
    const int32_t maxX = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x174) - 1;
    int32_t rect[4] = {};
    Virtual<void>(lot, 0x38, rect);
    bool neighbours = true;
    uint8_t sizeX = 0;
    uint8_t byte13 = 0; // the lot's depth first, then slot 42's flag
    if (static_cast<int32_t>(zone) >= 7 && static_cast<int32_t>(zone) <= 9) {
        Virtual<void>(lot, 0x58, &sizeX, &byte13);
        if (sizeX == 1 && byte13 == 1)
            neighbours = false;
    }
    void* const lots = At<void*>(self, local::kLots);
    TextureCell* begin = nullptr;
    TextureCell* end = nullptr;
    TextureCell* capacity = nullptr;
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            if (static_cast<uint32_t>(x) >= At<uint32_t>(self, local::kCellsX) ||
                static_cast<uint32_t>(z) >= At<uint32_t>(self, local::kCellsZ))
                continue;
            int32_t sides[4] = {1, 1, 1, 1};
            if (neighbours) {
                void* other = nullptr;
                if (z - 1 >= 0 && z - 1 <= maxZ)
                    other = Virtual<void*>(lots, 0x18, x, z - 1, 0u);
                if (other == lot)
                    sides[0] = 0;
                else if (Virtual<uint32_t>(lot, 0x5C) == 0)
                    sides[0] = 2;
                other = nullptr;
                if (z + 1 >= 0 && z + 1 <= maxZ)
                    other = Virtual<void*>(lots, 0x18, x, z + 1, 0u);
                if (other == lot)
                    sides[2] = 0;
                else if (Virtual<uint32_t>(lot, 0x5C) == 2)
                    sides[2] = 2;
                other = nullptr;
                if (x - 1 >= 0 && x - 1 <= maxX)
                    other = Virtual<void*>(lots, 0x18, x - 1, z, 0u);
                if (other == lot)
                    sides[3] = 0;
                else if (Virtual<uint32_t>(lot, 0x5C) == 3)
                    sides[3] = 2;
                other = nullptr;
                if (x + 1 >= 0 && x + 1 <= maxX)
                    other = Virtual<void*>(lots, 0x18, x + 1, z, 0u);
                if (other == lot)
                    sides[1] = 0;
                else if (Virtual<uint32_t>(lot, 0x5C) == 1)
                    sides[1] = 2;
            } else {
                sides[0] = sides[1] = sides[2] = sides[3] = 0;
            }
            TextureCell cell{};
            cell.x = static_cast<uint8_t>(x);
            cell.z = static_cast<uint8_t>(z);
            cell.byteD = 0xFF;
            Virtual<void>(self, 0xA8, sides, &cell.texture, &cell.orientation, &byte13);
            cell.color = color;
            cell.flag = byte13 != 0;
            cell.byteC = 3;
            // push_back with the game's growth: size + max(size, 1).
            if (end != capacity) {
                if (end)
                    *end = cell;
                ++end;
                continue;
            }
            const uint32_t size = static_cast<uint32_t>(end - begin);
            const uint32_t room = size + (size < 1 ? 1 : size);
            auto* const fresh = room ? static_cast<TextureCell*>(Allocate(room * sizeof(TextureCell))) : nullptr;
            TextureCell* out = fresh;
            for (TextureCell* in = begin; in != end; ++in, ++out)
                if (out)
                    *out = *in;
            if (out)
                *out = cell;
            if (begin)
                Free(begin);
            begin = fresh;
            end = out + 1;
            capacity = fresh + room;
        }
    }
    void* texture = nullptr;
    if (Virtual<uint8_t>(self, 0xA4, lot, &texture, 1u)) {
        Virtual<void>(texture, 0x20, begin, static_cast<int32_t>(end - begin), 0u);
        void* const occupant = Virtual<void*>(texture, 0xC);
        void* const occupants = At<void*>(self, local::kOccupants);
        if (Virtual<uint8_t>(occupant, 0x80, 1u)) {
            Virtual<void>(occupants, 0x60, occupant, 0u);
            Virtual<void>(occupant, 0x3C, 0x99EF1145u, 0u);
        } else {
            Virtual<void>(occupants, 0x50, occupant, 0u);
        }
        Release(texture);
    }
    if (begin)
        Free(begin);
}

// --- Flora of a lot -------------------------------------------------------------------------

// 0x006CD010. The position is not used.
uint32_t ChooseFloraType(void* self, uint32_t** candidates)
{
    if (candidates[0] == candidates[1])
        return 0;
    void* const random = reinterpret_cast<void*>(kRandom);
    const uint32_t pick =
        CallThis<uint32_t>(addr::cRZRandom_Below, random, static_cast<uint32_t>(candidates[1] - candidates[0]));
    const uint32_t type = candidates[0][pick];
    void* const flora = At<void*>(self, local::kFlora);
    if (Virtual<uint8_t>(flora, 0x2C, type))
        return type;
    HashSet set;
    ConstructSet(set);
    uint32_t result = 0;
    if (Virtual<uint32_t>(flora, 0x38, &set, type) && set.count) {
        uint32_t steps = CallThis<uint32_t>(addr::cRZRandom_Below, random, set.count);
        HashIterator at{};
        CallThis<void>(addr::SC4HashTable_Begin, &set, &at);
        HashNode* node = at.node;
        while (steps) {
            node = node->next;
            if (!node)
                node = CallThis<HashNode*>(kHashNextBucket, &at);
            --steps;
            at.node = node;
        }
        result = node->value;
    }
    DestroySet(set);
    return result;
}

// 0x006CD2B0. The third argument is not used.
void __fastcall PlantLotFlora(void* self, void*, void* lot, void* configuration, uint32_t)
{
    static bool logged = false;
    FirstCall("PlantLotFlora", logged);
    char** const objects = Virtual<char**>(configuration, 0x5C);
    if (!objects)
        return;
    int32_t count = static_cast<int32_t>(objects[1] - objects[0]) / 0x34;
    if (count <= 0)
        return;
    for (int32_t offset = 0; count; offset += 0x34, --count) {
        char* const object = objects[0] + offset;
        if (*object != 4)
            continue;
        const uint32_t origin = Global<uint32_t>(kFloraOrigin);
        uint32_t position[3] = {origin, origin, origin};
        uint32_t orientation = 0; // not set by the game before the call
        Virtual<void>(self, 0x9C, lot, configuration, object, position, &orientation);
        const uint32_t type = ChooseFloraType(self, reinterpret_cast<uint32_t**>(object + 0x28));
        if (!type)
            continue;
        void* const occupant = Virtual<void*>(At<void*>(self, local::kFlora), 0x44, type, position, 0u);
        if (occupant)
            Virtual<void>(occupant, 0x70, At<uint32_t>(object, 0x24));
    }
}

// --- The lot update ---------------------------------------------------------------------------

// 0x006CD3A0.
uint8_t UpdateLot(void* self, void* lot)
{
    static bool logged = false;
    FirstCall("UpdateLot", logged);
    uint8_t changed = 0;
    int32_t x = 0, z = 0;
    Virtual<void>(lot, 0x54, &x, &z);
    if (At<uint8_t>(self, local::kUpdateB8)) {
        Virtual<void>(lot, 0x78);
        if (Global<uint8_t>(kUpdateFlag) && Virtual<void*>(lot, 0xC0) && !Virtual<void*>(lot, 0xEC) &&
            !Virtual<uint8_t>(lot, 0x70)) {
            const int32_t zone = Virtual<int32_t>(lot, 0x7C);
            if (zone >= 7 && zone <= 9)
                CallThis<void>(kFn_006C4910, self, lot);
        }
    }
    void* const development = At<void*>(self, local::kDevelopment);
    if (const uint32_t type = Virtual<uint32_t>(lot, 0xB8, 0u)) {
        const uint32_t* const profile = Virtual<const uint32_t*>(development, 0x9C, type);
        if (*profile == 0) {
            const bool fine = CallThis<uint32_t>(kFn_006CABB0, self, lot, x, z) == 2;
            const uint32_t condition = Virtual<uint32_t>(lot, 0x8C);
            if (condition == 0) {
                if (!fine)
                    Virtual<void>(lot, 0x90, 1u);
            } else if (condition == 1) {
                if (fine)
                    Virtual<void>(lot, 0x90, 0u);
            } else if (condition == 5 || condition == 6) {
                Virtual<void>(lot, 0x90, 0u);
            }
            if (fine)
                CallThis<uint8_t>(kFn_006CAFC0, self, lot, profile, x, z);
        } else {
            const uint32_t state = Virtual<uint32_t>(lot, 0x84);
            uint32_t next = state;
            uint32_t reason = 0;
            if (!CallThis<uint8_t>(kFn_006C47C0, self, lot, x, z)) {
                next = 4;
                reason = 1;
            }
            if (!CallThis<uint32_t>(kFn_006CABB0, self, lot, x, z)) {
                next = 4;
                reason = 2;
            }
            if (!CallThis<uint8_t>(kFn_006CAE30, self, lot, x, z)) {
                next = 4;
                reason = 3;
            }
            if (!CallThis<uint8_t>(kFn_006CAFC0, self, lot, profile, x, z)) {
                next = 4;
                reason = 4;
            }
            if (next != state) {
                Virtual<void>(lot, 0x88, next, 0u, reason);
                changed = 1;
            }
            const uint32_t condition = CallThis<uint32_t>(kFn_006C4630, self, lot, x, z);
            Virtual<uint32_t>(lot, 0x8C);
            Virtual<void>(lot, 0x90, condition);
            if (Virtual<uint8_t>(development, 0xE8) && *profile == 2)
                CallThis<void>(kFn_006C78D0, self, lot);
        }
    } else if (At<uint8_t>(self, local::kUpdateBA)) {
        if (Virtual<uint32_t>(lot, 0xA0) > 0x1E) {
            const uint32_t zone = Virtual<uint32_t>(lot, 0x7C) - 1;
            if (zone <= 5 && At<uint8_t>(self, local::kDemand)) {
                uint32_t profile[0x10];
                CallThis<void*>(kProfileConstruct, static_cast<void*>(profile));
                profile[0] = zone <= 2 ? 1 : 3;
                CallThis<uint8_t>(kFn_006CAFC0, self, lot, static_cast<const uint32_t*>(profile), x, z);
                CallThis<void>(kProfileDestroy, static_cast<void*>(profile));
            }
        }
        if (CellShowsZot(lot, x, z))
            CallThis<uint32_t>(kFn_006CABB0, self, lot, x, z);
    }
    void* unit = Virtual<void*>(lot, 0x64);
    if (unit) {
        ListNode** const members = Virtual<ListNode**>(unit, 0x14);
        if (lot == (*members)->next->value && At<uint8_t>(self, local::kUpdateB9))
            Virtual<void>(self, 0x88, &unit);
    }
    unit = Virtual<void*>(lot, 0x64);
    if (unit) {
        ListNode** const members = Virtual<ListNode**>(unit, 0x14);
        if (lot == (*members)->next->value)
            Virtual<void>(self, 0x8C, unit);
    } else {
        Virtual<void>(lot, 0xE4);
        Virtual<void>(lot, 0xE8, lot);
        if (Virtual<uint8_t>(lot, 0x94) && !Virtual<uint8_t>(lot, 0x70))
            CallThis<void>(kUpdateUnit, self, lot);
    }
    return changed;
}

// The time budget of a pass (0x0068BF50 and the like; rdtsc based, made
// deterministic by determinism.fixed_ticks), checked every 0x400 steps.
struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};
static_assert(sizeof(Deadline) == 0x18);

// 0x006CD770.
uint8_t __fastcall StepLotUpdate(void* self, void*)
{
    static bool logged = false;
    FirstCall("StepLotUpdate", logged);
    using StartFn = void(__thiscall*)(Deadline* deadline, uint32_t budget);
    using CheckFn = uint8_t(__thiscall*)(Deadline* deadline);
    using EndFn = void(__thiscall*)(Deadline* deadline);
    void* const lots = At<void*>(self, local::kLots);
    const uint32_t state = At<uint32_t>(self, local::kUpdateState);
    if (state == 0 || state == 3) {
        void** const iterator = At<void**>(self, local::kLotIterator);
        if (!iterator) {
            At<void**>(self, local::kLotIterator) = Virtual<void**>(lots, 0x3C);
        } else if (void*** const list = static_cast<void***>(iterator[1])) {
            void** const sentinel = *list;
            iterator[0] = *sentinel;
        }
        if (Virtual<int32_t>(lots, 0x40) <= 0)
            return 1;
        At<int32_t>(self, local::kUpdateCount) = 0;
        At<uint8_t>(self, local::kUpdateB8) = At<uint8_t>(self, local::kDirty5D);
        At<uint8_t>(self, local::kUpdateB9) = At<uint8_t>(self, local::kDirty5E);
        At<uint8_t>(self, local::kDirty5E) = 0;
        At<uint8_t>(self, local::kDirty5D) = 0;
        const uint32_t date = Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20);
        At<uint8_t>(self, local::kUpdateBA) = date > At<uint32_t>(self, local::kDate);
        At<uint32_t>(self, local::kUpdateState) = 1;
    } else if (state != 1) {
        return 1;
    }
    uint32_t& once = Global<uint32_t>(kUpdateStepOnce);
    if (!(once & 1)) {
        once |= 1;
        Global<uint32_t>(kUpdateStep) = 0x400;
        Global<uint32_t>(kUpdateStep + 4) = 0;
    }
    Deadline deadline{};
    reinterpret_cast<StartFn>(addr::SC4Deadline_Start)(&deadline, 5);
    const uint32_t step = Global<uint32_t>(kUpdateStep);
    deadline.count = 0;
    deadline.step = step;
    deadline.timedOut = 0;
    deadline.stepGlobal = &Global<uint32_t>(kUpdateStep);
    const int32_t lotCount = Virtual<int32_t>(lots, 0x40);
    if (lotCount <= 0) {
        At<uint32_t>(self, local::kUpdateState) = 3;
        reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
        return 1;
    }
    const int32_t share = lotCount / 28 + 1;
    if (At<int32_t>(self, local::kUpdateCount) < share) {
        uint32_t count = 0;
        for (;;) {
            count += step;
            deadline.count = count;
            if (!(count & 0x3FF) && reinterpret_cast<CheckFn>(addr::SC4Deadline_Expired)(&deadline)) {
                deadline.timedOut = 1;
                break;
            }
            void** const iterator = At<void**>(self, local::kLotIterator);
            void** const list = static_cast<void**>(iterator[1]);
            if (!list || iterator[0] == *list) {
                At<uint32_t>(self, local::kUpdateState) = 3;
                Global<uint8_t>(kUpdateFlag) = 0;
                break;
            }
            void** const current = static_cast<void**>(iterator[0]);
            iterator[0] = current[0];
            UpdateLot(self, current[2]);
            if (++At<int32_t>(self, local::kUpdateCount) >= share)
                break;
        }
    }
    if (At<int32_t>(self, local::kUpdateCount) < share) {
        reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
        return 0;
    }
    At<int32_t>(self, local::kUpdateCount) = 0;
    reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
    return 1;
}

// --- Installation -----------------------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(6, 0x006CC490, StartConstruction),
    OPENSC4_SLOT(7, 0x006CCEF0, ConstructDefaultLot),
    OPENSC4_SLOT(11, 0x006CB940, RemoveBuildingZotProp),
    OPENSC4_SLOT(12, 0x006CC1D0, StartDemolishLot),
    OPENSC4_SLOT(14, 0x006CC250, ExplodeLot),
    OPENSC4_SLOT(34, 0x006CB4D0, UpdateTravelDesignates),
    OPENSC4_SLOT(37, 0x006CC880, UpdateLotElements),
    OPENSC4_SLOT(43, 0x006CC970, SetEmptyLotBaseTexture),
    OPENSC4_SLOT(64, 0x006CB760, GetLotConfigurationObjectOccupant),
};

#undef OPENSC4_SLOT

struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

Detour g_detours[] = {
    {kStepNightBuildings, reinterpret_cast<void*>(&StepNightBuildings), nullptr},
    {kFinishNightBuildings, reinterpret_cast<void*>(&FinishNightBuildings), nullptr},
    {kUpdatePowerInRect, reinterpret_cast<void*>(&UpdatePowerInRect), nullptr},
    {kPlantLotFlora, reinterpret_cast<void*>(&PlantLotFlora), nullptr},
    {kStepLotUpdate, reinterpret_cast<void*>(&StepLotUpdate), nullptr},
};

// Every slot must still hold the game's function; then the five detours,
// then the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(kVtablePrimary);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("lot developer construct: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(kVtablePrimary), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* const entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("lot developer construct: no detour at %08X; not installing",
                       static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("lot developer construct: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.lot.construct", "simulators/lot", registry::Kind::Vtable, registry::Phase::PostAppInit, kVtablePrimary,
     "Our cSC4LotDeveloper construction and demolition: StartConstruction, ConstructDefaultLot, StartDemolishLot, "
     "ExplodeLot, UpdateLotElements, the empty lot's base texture, travel designates, the power and night steps, "
     "a lot's flora and the lot update (src/core/lot_construct.cpp)",
     false, Install});

} // namespace
} // namespace opensc4::lot_sim
