// Our cSC4ConstructionOccupant (build 641; the class map, the vtables slot
// by slot and the fields are in constrocc_sim.h): identity and the box in
// the class's vtables, the simulation's two functions (SetLot with the
// construction's schedule, and the step that follows the days), the save
// record's Write and Read, and our own object (the constructor, the factory
// and the pool allocation by detour, the destructor and identity from
// copies of the vtables).
//
// Nearly all the rest of the class draws or animates (the scaffolding's
// geometry and textures, the crane, props and effects, the renderer's
// interface; Init, Shutdown and DoMessage with them): another thread's, left
// the game's. Where SetLot and the step need those functions they call them
// at their addresses, in the game's order.
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Write, Read) is every occupant class's
// (src/core/occbase_sim.cpp): it is called at its address.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the object's slot 0x4 (AddRef 0x006491C0, a jump to the base's): none;
//   - the occupant part 0x00AA21B8: 0xC Init 0x005D1300, 0x10 Shutdown
//     0x005D0CE0, 0x14 IsInitialized 0x00458A40: none;
//   - the lot 0x00AB19B0: 0x4 AddRef 0x004664A0 and 0x8 Release 0x006BF460
//     none, 0x88 SetState 0x006C3100 three, 0xC0 GetBuilding 0x006BFA30
//     none;
//   - the building occupant 0x00A872C0: 0x4 and 0x8 (0x005E28F0,
//     0x005E2900) none, 0x2C GetBuildingProfile 0x00459250 none;
//   - the simulator 0x00AB2FB8: 0x20 (0x00703D40, a jump to its slot 0x68,
//     the day number) none;
//   - a prop occupant (interface 0xE9793A65; 0x00AA4730 and six more): 0xC
//     AsOccupant 0x006C2040 none, 0x8 Release none;
//   - the prop manager 0x00A8F928: 0x74 (0x004A27B0) one;
//   - a visual effect prop 0x00AA4ED8: 0x20 StopEffect 0x005EDAA0 one;
//   - the renderer 0x00ABAB60: 0x74 RemoveModel 0x007CA3D0 one;
//   - the occupant manager 0x00A8F7F0: 0x54 RemoveOccupant 0x0049F240 three;
//   - the streams 0x00AA2310 (out; 0x005D1D80, 0x005D1DB0, 0x005D1E30,
//     0x005D1EB0, 0x005D1F70) and 0x00AA2280 (in; 0x005D1930, 0x005D1960,
//     0x005D19C0, 0x005D1A20, 0x005D1AB0): slots 0x10, 0x14, 0x1C, 0x24,
//     0x30 one argument, 0x48 (0x005D16B0) none.
#include "core/constrocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace opensc4::constrocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("constrocc: first %s", name);
}

namespace {

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

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x005CA7A0): the construction interface here (the object, counted
// through its slot 1), everything else from the occupant base (a jump, with
// the object + 8). The class ID is not answered here.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidConstruction) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x005D04B0, 0x005D04C0, 0x005D04D0, 0x005D04E0,
// 0x005D0500).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// The virtual base's slot 0 (0x005D0520): the pointer less the displacement
// word before it (zero in a whole object), less 0x19C.
uint8_t __fastcall QueryInterfaceAtModelBase(char* part, void*, uint32_t iid, void** object)
{
    int32_t displacement;
    std::memcpy(&displacement, part - 4, 4);
    return QueryInterface(part - displacement - field::kModelBase, nullptr, iid, object);
}

// The days of one short stage, as SetLot works them out: the value plus
// 12582912.0 stored as a float, whose bits less 0x4B400000 are the value
// rounded to an integer (the game's fast rounding; the sum is made on the
// FPU, where the caller's product already is), and of that and 1 the
// smaller (signed).
int32_t StageDays(double value)
{
    const float sum = static_cast<float>(value + static_cast<double>(Global<float>(addr::cSC4ConstructionOccupant_RoundMagic)));
    const int32_t rounded = static_cast<int32_t>(FloatBits(sum) + 0xB4C00000u);
    return rounded > 1 ? 1 : rounded;
}

// Slot 4 (0x005D0890): a construction's start for a lot.
//   - The occupant part is initialised when it is not yet (its slots 5 and
//     3).
//   - The lot is kept (the new one referenced before the old one is
//     released) and the building let go. Without a lot only the model's
//     place in the renderer is brought up to date (0x005CA960).
//   - The lot's building (lot slot 48) is kept; without one the lot is let
//     go again and that is all.
//   - The drawing's setup, the game's functions in the game's order:
//     0x005CAD60, eight 0x18-byte records constructed (0x0041D490 with
//     0x00466E10) and handed to 0x005CB9F0, 0x005CE360 and 0x005CF790, a
//     copy of the box at +0xC0 with the records to 0x005CFA60 and alone to
//     0x005CFC40, then 0x005CBC10, 0x005CEC20, 0x005CF380. They set the
//     boxes, the floors (+0x150) and the first height (+0x15C) the schedule
//     below uses.
//   - The schedule: today (+0x144); the building profile's construction
//     days (byte +0xA, 10 for 0); the first stage's days from its height
//     above the box's floor, the pause between floors from the float at
//     0x00B0A7C8 (both times the float at 0x00B0A7D8), the last stage's
//     from the box's height (times 0x00B0A7DC), each at most 1 by
//     StageDays; what is left of the total after the pauses of every floor
//     and the two ends, but at least the floor count (signed compare),
//     divided by the floor count (unsigned): the days per floor. Stage 0,
//     no floor started, the next step due after the first stage's days.
//     A floor count of zero divides by zero as in the game.
//   - The building's completion from the float at 0x00B22F94 (0x005CA8D0),
//     the height just above the box's floor (0x005CC020 with the floor's y
//     plus the double 0.001, as a float), the crane's fields from +0x178
//     and 0x005CBE10 when there is a crane (+0x104), the two messages
//     0x49E7B1E2 and 0xC9E7B1E8 with 1, the renderer brought up to date.
void __fastcall SetLot(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("SetLot", logged);
    char* const part = self + field::kOccupant;
    if (!Virtual<uint8_t>(part, occupant::kIsInitialized))
        Virtual<uint8_t>(part, occupant::kInit);
    void* const oldLot = At<void*>(self, field::kLot);
    if (lot != oldLot) {
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        At<void*>(self, field::kLot) = lot;
        if (oldLot)
            Virtual<uint32_t>(oldLot, 0x8);
    }
    if (void* const oldBuilding = At<void*>(self, field::kBuilding)) {
        At<void*>(self, field::kBuilding) = nullptr;
        Virtual<uint32_t>(oldBuilding, 0x8);
    }
    if (At<void*>(self, field::kLot)) {
        void* const building = Virtual<void*>(At<void*>(self, field::kLot), 0xC0);
        if (!building) {
            if (void* const kept = At<void*>(self, field::kLot)) {
                At<void*>(self, field::kLot) = nullptr;
                Virtual<uint32_t>(kept, 0x8);
            }
            return;
        }
        void* const previous = At<void*>(self, field::kBuilding);
        if (building != previous) {
            Virtual<uint32_t>(building, 0x4);
            At<void*>(self, field::kBuilding) = building;
            if (previous)
                Virtual<uint32_t>(previous, 0x8);
        }
        CallThis<void>(addr::cSC4ConstructionOccupant_AllocateGeometry, self);
        alignas(4) unsigned char records[8 * 0x18];
        reinterpret_cast<void(__stdcall*)(void*, uint32_t, int32_t, void*)>(addr::SC4UI_F41D490)(
            records, 0x18, 8, reinterpret_cast<void*>(addr::SC4Model_ConstructCorner));
        CallThis<void, void*>(addr::cSC4ConstructionOccupant_Setup5CB9F0, self, static_cast<void*>(records));
        CallThis<void, void*>(addr::cSC4ConstructionOccupant_Setup5CE360, self, static_cast<void*>(records));
        CallThis<void, void*>(addr::cSC4ConstructionOccupant_Setup5CF790, self, static_cast<void*>(records));
        uint32_t box[6];
        std::memcpy(box, self + field::kInnerBox, sizeof box);
        CallThis<void, void*, void*>(addr::cSC4ConstructionOccupant_Setup5CFA60, self, static_cast<void*>(records),
                                     static_cast<void*>(box));
        CallThis<void, void*>(addr::cSC4ConstructionOccupant_Setup5CFC40, self, static_cast<void*>(box));
        CallThis<void>(addr::cSC4ConstructionOccupant_Setup5CBC10, self);
        CallThis<void>(addr::cSC4ConstructionOccupant_Setup5CEC20, self);
        CallThis<void>(addr::cSC4ConstructionOccupant_Setup5CF380, self);
        At<uint32_t>(self, field::kStartDay) = Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20);
        char* const profile = Virtual<char*>(At<void*>(self, field::kBuilding), 0x2C);
        uint32_t total = At<uint8_t>(profile, 0xA);
        if (total == 0)
            total = 10;
        const double perHeight = static_cast<double>(Global<float>(addr::cSC4ConstructionOccupant_DaysPerHeight));
        const double floorY = static_cast<double>(At<float>(self, field::kBox + 0x4));
        const int32_t firstDays = StageDays((static_cast<double>(At<float>(self, field::kFirstHeight)) - floorY) * perHeight);
        At<int32_t>(self, field::kFirstDays) = firstDays;
        At<int32_t>(self, field::kPauseDays) =
            StageDays(static_cast<double>(Global<float>(addr::cSC4ConstructionOccupant_FloorPause)) * perHeight);
        const int32_t lastDays = StageDays((static_cast<double>(At<float>(self, field::kBox + 0x10)) - floorY) *
                                           static_cast<double>(Global<float>(addr::cSC4ConstructionOccupant_DaysPerHeightEnd)));
        const uint32_t floors = At<uint32_t>(self, field::kFloors);
        uint32_t left = total - At<uint32_t>(self, field::kPauseDays) * floors - static_cast<uint32_t>(firstDays) -
                        static_cast<uint32_t>(lastDays);
        At<int32_t>(self, field::kLastDays) = lastDays;
        if (static_cast<int32_t>(left) < static_cast<int32_t>(floors))
            left = floors;
        const uint32_t floorDays = left / floors;
        At<int32_t>(self, field::kStage) = 0;
        At<uint32_t>(self, field::kFloor) = 0;
        At<uint32_t>(self, field::kElapsed) = 0;
        At<uint32_t>(self, field::kFloorDays) = floorDays;
        At<uint32_t>(self, field::kNextDay) = At<uint32_t>(self, field::kStartDay) + static_cast<uint32_t>(firstDays);
        CallThis<void, const void*>(addr::cSC4ConstructionOccupant_SetCompletion, self,
                                    reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_StartCompletion));
        const float height = static_cast<float>(static_cast<double>(At<float>(self, field::kBox + 0x4)) +
                                                Global<double>(addr::NeighborTuning_YearsSlack));
        CallThis<void, uint32_t>(addr::cSC4ConstructionOccupant_SetHeight, self, FloatBits(height));
        if (At<uint32_t>(self, field::kCrane) != 0) {
            const uint32_t angle = At<uint32_t>(self, field::kCraneAngle);
            At<uint32_t>(self, field::kCraneTarget + 0x4) = At<uint32_t>(self, field::kCraneAngle + 0x4);
            At<uint32_t>(self, field::kCraneTarget) = angle;
            At<uint32_t>(self, field::kCraneTime) = 1;
            At<uint32_t>(self, field::kCraneTime + 0x4) = 0;
            At<int32_t>(self, field::kCraneState) = 2;
            CallThis<void, uint32_t>(addr::cSC4ConstructionOccupant_Setup5CBE10, self, angle);
        }
        CallThis<void, uint32_t, uint32_t>(addr::cSC4ConstructionOccupant_SendMessage, self, 0x49E7B1E2u, 1u);
        CallThis<void, uint32_t, uint32_t>(addr::cSC4ConstructionOccupant_SendMessage, self, 0xC9E7B1E8u, 1u);
    }
    CallThis<void>(addr::cSC4ConstructionOccupant_UpdateDrawn, self);
}

// The construction's step (0x005D0680; DoMessage calls it on the
// simulator's message 0x66956814). Nothing without a building, in stage 4,
// or before the day of the next step (unsigned). Otherwise the elapsed time
// is zeroed and by the stage:
//   0, 2 (a start, a floor done): one more floor started. While that is
//     within the floor count: message 0x49E7B1E2 with 1, stage 1, the next
//     step after the pause. Past it: stage 3, the next step after the last
//     stage's days, every prop of the second list taken from the prop
//     manager (its slot 29 with the prop's occupant part) and released, the
//     list emptied, the lone effect stopped (its slot 8 with 1). Both ways
//     every effect of the vector is then stopped.
//   1 (a pause done): stage 2, the next step after a floor's days, and the
//     effects moved and started again (0x005CFE20).
//   3 (the end): stage 4, the building's completion from the float at
//     0x00B0A7F0 (0x005CA8D0); the model taken out of the renderer when the
//     occupant's flag 2 says it is in (the flag cleared); the occupant
//     removed from the occupant manager (its slot 21 with the occupant
//     part, 0, 0); the lot set to state 3 (its slot 34 with 3, 0, 0); the
//     occupant shut down (its slot 4).
// The prop manager is read once before the loop and not checked, the
// list's head again at every turn, as in the game.
void __fastcall Step(char* self, void*)
{
    static bool logged = false;
    FirstCall("Step", logged);
    if (!At<void*>(self, field::kBuilding))
        return;
    if (At<int32_t>(self, field::kStage) == 4)
        return;
    const uint32_t today = Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20);
    if (today < At<uint32_t>(self, field::kNextDay))
        return;
    const uint32_t stage = At<uint32_t>(self, field::kStage);
    At<uint32_t>(self, field::kElapsed) = 0;
    switch (stage) {
    case 0:
    case 2: {
        const uint32_t floor = At<uint32_t>(self, field::kFloor) + 1;
        const uint32_t floors = At<uint32_t>(self, field::kFloors);
        At<uint32_t>(self, field::kFloor) = floor;
        if (floor <= floors) {
            CallThis<void, uint32_t, uint32_t>(addr::cSC4ConstructionOccupant_SendMessage, self, 0x49E7B1E2u, 1u);
            At<int32_t>(self, field::kStage) = 1;
            At<uint32_t>(self, field::kNextDay) = At<uint32_t>(self, field::kPauseDays) + today;
        } else {
            At<int32_t>(self, field::kStage) = 3;
            At<uint32_t>(self, field::kNextDay) = At<uint32_t>(self, field::kLastDays) + today;
            void* const propManager = Global<void*>(addr::SC4_Service43D10);
            for (char* node = At<char*>(At<char*>(self, field::kStageProps), 0x0);
                 node != At<char*>(self, field::kStageProps); node = At<char*>(node, 0x0)) {
                void* const prop = At<void*>(node, 0x8);
                void* const propOccupant = Virtual<void*>(prop, 0xC);
                Virtual<void>(propManager, 0x74, propOccupant);
                Virtual<uint32_t>(prop, 0x8);
            }
            CallThis<void>(addr::SC4StlList_Clear, self + field::kStageProps);
            Virtual<void>(At<void*>(self, field::kEffect), 0x20, 1u);
        }
        for (uint32_t i = 0;
             i < static_cast<uint32_t>((At<char*>(self, field::kEffects + 0x4) - At<char*>(self, field::kEffects)) >> 2); ++i)
            Virtual<void>(At<void**>(self, field::kEffects)[i], 0x20, 1u);
        return;
    }
    case 1:
        At<uint32_t>(self, field::kNextDay) = At<uint32_t>(self, field::kFloorDays) + today;
        At<int32_t>(self, field::kStage) = 2;
        CallThis<void>(addr::cSC4ConstructionOccupant_MoveEffects, self);
        return;
    case 3: {
        At<int32_t>(self, field::kStage) = 4;
        CallThis<void, const void*>(addr::cSC4ConstructionOccupant_SetCompletion, self,
                                    reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_EndCompletion));
        if (At<uint8_t>(self, field::kFlags) & 2) {
            char* const modelBase = self + field::kVbtable + At<int32_t>(At<char*>(self, field::kVbtable), 0x4);
            Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x74, modelBase);
            At<uint8_t>(self, field::kFlags) &= 0xFD;
        }
        char* const part = self + field::kOccupant;
        Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x54, part, 0u, 0u);
        Virtual<void>(At<void*>(self, field::kLot), 0x88, 3u, 0u, 0u);
        Virtual<uint8_t>(part, occupant::kShutdown);
        return;
    }
    default:
        return;
    }
}

// --- The occupant part (`part` is the object + 8) ---------------------------------

// Slot 8 (0x005CA7D0): the middle of the box: per axis the high plus the
// low, times the float 0.5, made on the FPU and stored as a float. EAX is
// left holding the buffer.
float* __fastcall GetPosition(char* part, void*, float* position)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    char* const self = part - field::kOccupant;
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    for (int i = 0; i < 3; ++i)
        position[i] = static_cast<float>((static_cast<double>(At<float>(self, field::kBox + 0xC + 4 * i)) +
                                          static_cast<double>(At<float>(self, field::kBox + 4 * i))) *
                                         half);
    return position;
}

// Slot 10 (0x005CA820): the box copied as it is. EAX is left holding the
// second buffer.
void* __fastcall GetBoundingBox(char* part, void*, void* low, void* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    std::memcpy(low, self + field::kBox, 12);
    std::memcpy(high, self + field::kBox + 0xC, 12);
    return high;
}

// --- The other parts ----------------------------------------------------------------

// +0xC slot 5 (0x005D04A0).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

struct ListNode {
    ListNode* next;
    ListNode* previous;
};

uint32_t ListCount(const ListNode* head)
{
    uint32_t count = 0;
    for (const ListNode* node = head->next; node != head; node = node->next)
        ++count;
    return count;
}

// A node of `size` bytes from the game's allocator, linked in before the
// list's sentinel (the game's inline push_back); the caller fills it from
// +8 on, as the game does before linking.
char* NewNode(std::size_t size) { return CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, size); }

void LinkBack(ListNode* head, char* node)
{
    auto* const item = reinterpret_cast<ListNode*>(node);
    ListNode* const last = head->previous;
    item->next = head;
    item->previous = last;
    last->next = item;
    head->previous = item;
}

// +0xC slot 3 (0x005CD250; `part` = object + 0xC): version 5, the occupant
// base's record, then the class's fields in the order of
// tools/re/save_schemas/constrocc.py. The stage and the crane's state go
// out as one byte each; the visual effects as references to their parts
// at +4 (the vector's) and +0x10 (the lone one), as the game writes them;
// the lists' heads are read again at every turn but the last list's. When
// the base fails the class's own fields are left out, and the result is in
// both cases whether the stream is free of error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 5u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kLot));
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kBuilding));
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteTransform, stream, self + field::kTransform);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteBox, stream, self + field::kBox);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteBox, stream, self + field::kInnerBox);
        CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream,
                                          At<uint32_t>(self, field::kCrane) != 0 ? 1u : 0u);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kCranePlace);
        for (const std::size_t list : {field::kProps, field::kStageProps}) {
            Virtual<uint8_t>(stream, 0x24, ListCount(At<ListNode*>(self, list)));
            for (ListNode* node = At<ListNode*>(self, list)->next; node != At<ListNode*>(self, list); node = node->next)
                CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(node, 0x8));
        }
        Virtual<uint8_t>(stream, 0x24,
                         static_cast<uint32_t>((At<char*>(self, field::kEffects + 0x4) - At<char*>(self, field::kEffects)) >> 2));
        for (char** effect = At<char**>(self, field::kEffects); effect != At<char**>(self, field::kEffects + 0x4); ++effect)
            CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream,
                                             *effect ? static_cast<void*>(*effect + 0x4) : nullptr);
        char* const effect = At<char*>(self, field::kEffect);
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream,
                                         effect ? static_cast<void*>(effect + 0x10) : nullptr);
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kStage)});
        for (std::size_t offset = field::kStartDay; offset <= field::kNextDay; offset += 4)
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, offset));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kFloors));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kFloor));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kHeight158));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kFirstHeight));
        for (std::size_t offset = field::kFirstDays; offset <= field::kElapsed; offset += 4)
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, offset));
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kCraneState)});
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kCraneAngle));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kCraneAngle + 0x4));
        CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(self, field::kCraneBusy)});
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kCraneTime));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kCraneTime + 0x4));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kCraneTarget));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kCraneTarget + 0x4));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kSides)});
        ListNode* const network = At<ListNode*>(self, field::kNetwork);
        Virtual<uint8_t>(stream, 0x24, ListCount(network));
        for (ListNode* node = network->next; node != network; node = node->next) {
            CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(node, 0x8)});
            CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(node, 0x9)});
            CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(node, 0xC));
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0xC slot 4 (0x005CE770; `part` = object + 0xC): versions 4 and 5 (4 has
// nine floats for the transform's matrix). An older version: AL cleared,
// the rest of EAX as the stream's read left it. The loaded mark (+0xD9) is
// set before the base is read; a failing base skips the fields, and the
// result is the stream's state as in Write.
//   - The lot and the building are released when set, then read as
//     references (interfaces 0xE818FCED and 0x87DDA3A9); the lone effect is
//     read over what is there (no release), as in the game.
//   - The bool Write made of +0x104 lands in +0xD8.
//   - The stage and the crane's state come back as signed bytes.
//   - Each list's items are appended in the order read; the effects'
//     vector is reserved to the count (0x0045D4F0), then filled; the grow
//     path of the game's inline push_back (double, at least one) is kept
//     though the reserve makes it unreachable.
// The game's locals it never initialises are zero here: the version word,
// the reference read for the two prop lists (one stack slot for both), the
// effect read for the vector, and the crane state's byte. The stage's byte
// is read into the argument's stack slot, so it starts as the stream
// pointer's low byte. The network list's nodes get the dword of the stack
// slot that holds the two bools: its upper two bytes are those of the
// slot's last use, the effects' loop counter, which ended at the count.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = 0;
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) < 4)
        return read & 0xFFFFFF00u;
    At<uint8_t>(self, field::kLoaded) = 1;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        if (void* const lot = At<void*>(self, field::kLot)) {
            At<void*>(self, field::kLot) = nullptr;
            Virtual<uint32_t>(lot, 0x8);
        }
        CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, 0xE818FCEDu,
                                                            self + field::kLot, 0u);
        if (void* const building = At<void*>(self, field::kBuilding)) {
            At<void*>(self, field::kBuilding) = nullptr;
            Virtual<uint32_t>(building, 0x8);
        }
        CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, 0x87DDA3A9u,
                                                            self + field::kBuilding, 0u);
        CallCdecl<void*, void*, void*>(static_cast<uint16_t>(version) < 5 ? addr::SC4Stream_ReadMatrixAsTransform
                                                                           : addr::SC4Stream_ReadTransform,
                                       stream, self + field::kTransform);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBox, stream, self + field::kBox);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBox, stream, self + field::kInnerBox);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, self + field::kHadTexture);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kCranePlace);
        void* prop = nullptr;
        for (const std::size_t list : {field::kProps, field::kStageProps}) {
            uint32_t count = 0;
            Virtual<uint8_t>(stream, 0x24, &count);
            for (uint32_t i = 0; i < count; ++i) {
                CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream,
                                                                    0xE9793A65u, &prop, 0u);
                ListNode* const head = At<ListNode*>(self, list);
                char* const node = NewNode(0xC);
                At<void*>(node, 0x8) = prop;
                LinkBack(head, node);
            }
        }
        uint32_t effects = 0;
        Virtual<uint8_t>(stream, 0x24, &effects);
        char* const vector = self + field::kEffects;
        CallThis<void, uint32_t>(addr::PtrVector_Reserve, vector, effects);
        uint32_t index = 0;
        void* effect = nullptr;
        while (index < effects) {
            CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, 0x2977AA49u,
                                                                &effect, 0u);
            void** const end = At<void**>(vector, 0x4);
            if (end != At<void**>(vector, 0x8)) {
                if (end)
                    *end = effect;
                At<void**>(vector, 0x4) = end + 1;
            } else {
                const uint32_t size = static_cast<uint32_t>(end - At<void**>(vector, 0x0));
                const uint32_t capacity = size + (size < 1 ? 1u : size);
                void** const fresh =
                    capacity ? CallCdecl<void**, std::size_t>(addr::SC4_stl_allocate, capacity * 4) : nullptr;
                void** const begin = At<void**>(vector, 0x0);
                void** place = fresh;
                if (end != begin) {
                    const std::size_t bytes = static_cast<std::size_t>(reinterpret_cast<char*>(end) -
                                                                       reinterpret_cast<char*>(begin));
                    CallCdecl<void*, void*, void*, std::size_t>(addr::SC4_memmove, fresh, begin, bytes);
                    place = reinterpret_cast<void**>(reinterpret_cast<char*>(fresh) + bytes);
                }
                *place = effect;
                if (void* const old = At<void*>(vector, 0x0))
                    CallCdecl<void, void*>(addr::SC4_Free, old);
                At<void**>(vector, 0x0) = fresh;
                At<void**>(vector, 0x4) = place + 1;
                At<void**>(vector, 0x8) = fresh + capacity;
            }
            ++index;
        }
        CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, 0x2977AA49u,
                                                            self + field::kEffect, 0u);
        uint32_t byteSlot = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
        Virtual<uint8_t>(stream, 0x10, &byteSlot);
        At<int32_t>(self, field::kStage) = static_cast<int8_t>(byteSlot);
        for (std::size_t offset = field::kStartDay; offset <= field::kNextDay; offset += 4)
            Virtual<uint8_t>(stream, 0x24, self + offset);
        Virtual<uint8_t>(stream, 0x24, self + field::kFloors);
        Virtual<uint8_t>(stream, 0x24, self + field::kFloor);
        Virtual<uint8_t>(stream, 0x30, self + field::kHeight158);
        Virtual<uint8_t>(stream, 0x30, self + field::kFirstHeight);
        for (std::size_t offset = field::kFirstDays; offset <= field::kElapsed; offset += 4)
            Virtual<uint8_t>(stream, 0x24, self + offset);
        int8_t craneState = 0;
        Virtual<uint8_t>(stream, 0x10, &craneState);
        At<int32_t>(self, field::kCraneState) = craneState;
        Virtual<uint8_t>(stream, 0x30, self + field::kCraneAngle);
        Virtual<uint8_t>(stream, 0x30, self + field::kCraneAngle + 0x4);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, self + field::kCraneBusy);
        Virtual<uint8_t>(stream, 0x24, self + field::kCraneTime);
        Virtual<uint8_t>(stream, 0x24, self + field::kCraneTime + 0x4);
        Virtual<uint8_t>(stream, 0x30, self + field::kCraneTarget);
        Virtual<uint8_t>(stream, 0x30, self + field::kCraneTarget + 0x4);
        Virtual<uint8_t>(stream, 0x14, self + field::kSides);
        uint32_t pieces = 0;
        Virtual<uint8_t>(stream, 0x24, &pieces);
        struct {
            uint32_t bools;
            void* occupant;
        } pair = {index, nullptr};
        for (uint32_t i = 0; i < pieces; ++i) {
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, reinterpret_cast<char*>(&pair.bools));
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, reinterpret_cast<char*>(&pair.bools) + 1);
            CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, 0xA9B40F05u,
                                                                &pair.occupant, 0u);
            ListNode* const head = At<ListNode*>(self, field::kNetwork);
            char* const node = NewNode(0x10);
            At<uint32_t>(node, 0x8) = pair.bools;
            At<void*>(node, 0xC) = pair.occupant;
            LinkBack(head, node);
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x005D0660, 0x005D0670): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable, 0, addr::cSC4ConstructionOccupant_QueryInterface,
                 QueryInterface),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable, 4, addr::cSC4ConstructionOccupant_SetLot, SetLot),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_4, 0, addr::cSC4ConstructionOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_8, 0, addr::cSC4ConstructionOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_8, 8, addr::cSC4ConstructionOccupant_GetPosition,
                 GetPosition),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_8, 10, addr::cSC4ConstructionOccupant_GetBoundingBox,
                 GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_C, 0, addr::cSC4ConstructionOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_C, 5, addr::cSC4ConstructionOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_10, 0, addr::cSC4ConstructionOccupant_QueryInterfaceThunk10,
                 QueryInterfaceAt<0x10>),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_24, 0, addr::cSC4ConstructionOccupant_QueryInterfaceThunk24,
                 QueryInterfaceAt<0x24>),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_19C, 0, addr::cSC4ConstructionOccupant_QueryInterfaceThunk19C,
                 QueryInterfaceAtModelBase),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_C, 3, addr::cSC4ConstructionOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_C, 4, addr::cSC4ConstructionOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_10, 23, addr::cSC4ConstructionOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4ConstructionOccupant_vftable_10, 24, addr::cSC4ConstructionOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("constrocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(slots, what))
        return false;
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("constrocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

using StepFn = void(__fastcall*)(char*, void*);
StepFn g_originalStep = nullptr;

// The slots are reached through the vtables only (the QueryInterface thunks
// jump to 0x005CA7A0 and the save thunks to Write and Read inside the
// class; the class registration's call of GetGZCLSID at 0x004607EA runs
// before any replacement is installed) and no other class's vtable holds
// one, so they are slot swaps. The step is no slot: the game's DoMessage
// calls it directly at 0x005D1225 (lea ecx, [esi - 4]; call; then pop edi,
// pop esi, mov al 1, pop ebp, ret 4: nothing read from EAX, ECX, EDX or the
// flags after), its only caller, so it is a detour, installed first (a
// failure leaves the vtables untouched).
bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(kSlots, "slots"))
        return false;
    const addr::Entry* const step = addr_check::Find(addr::cSC4ConstructionOccupant_Step);
    if (!step || !hook::Install(*step, &Step, &g_originalStep))
        return false;
    return SwapSlots(kSlots, "slots");
}

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.constrocc.slots", "simulators/constrocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ConstructionOccupant_vftable,
                        "Our cSC4ConstructionOccupant slots: QueryInterface and the parts' thunks, SetLot (the "
                        "construction's schedule), GetPosition, GetBoundingBox, GetGZCLSID, and the daily step by "
                        "detour (src/core/constrocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.constrocc.save", "simulators/constrocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ConstructionOccupant_vftable_C,
                        "Our cSC4ConstructionOccupant Write and Read (the save record, version 5; "
                        "src/core/constrocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's eight vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 5, kSlots4 = 4, kSlots8 = 34, kSlotsC = 6, kSlots10 = 25, kSlots24 = 5, kSlots3C = 1,
              kSlots19C = 19;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable24[kSlots24];
void* g_vtable3C[kSlots3C];
void* g_vtable19C[kSlots19C];

// An item of the construction pool (0x005CA8A0, and inline in the factory):
// the current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4ConstructionOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x005D0C00) with the destructor (0x005D0C60)
// inline. The destructor stores no vtable (the game's does not either): the
// network list freed (0x005650A0), the effects' buffer, the two prop lists,
// the texture rows (0x004638B0), the building and the lot released, the
// model part and then the occupant base destroyed at their addresses. With
// flag 1 the memory goes back to the pool (the page header is at the
// address with its low 15 bits clear).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kNetwork);
    if (void* const buffer = At<void*>(self, field::kEffects))
        CallCdecl<void, void*>(addr::SC4_Free, buffer);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kStageProps);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kProps);
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, self + field::kRows);
    if (void* const building = At<void*>(self, field::kBuilding))
        Virtual<uint32_t>(building, 0x8);
    if (void* const lot = At<void*>(self, field::kLot))
        Virtual<uint32_t>(lot, 0x8);
    CallThis<void>(addr::SC4Model_DestroyModelPart, self + field::kModelPart);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFF8000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4ConstructionOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x4000003A)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x005D04F0) and the model part's slot 0
// (0x005D0510).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void* __fastcall DeletingDestructorAtModelPart(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kModelPart, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_10),
                sizeof(g_vtable10));
    std::memcpy(g_vtable24, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_24),
                sizeof(g_vtable24));
    std::memcpy(g_vtable3C, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_3C),
                sizeof(g_vtable3C));
    std::memcpy(g_vtable19C, reinterpret_cast<const void*>(addr::cSC4ConstructionOccupant_vftable_19C),
                sizeof(g_vtable19C));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable10[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x10>);
    g_vtable10[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable24[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x24>);
    g_vtable3C[0] = reinterpret_cast<void*>(&DeletingDestructorAtModelPart);
    g_vtable19C[0] = reinterpret_cast<void*>(&QueryInterfaceAtModelBase);
    log::Write("constrocc object: vtables copied (slot 4 %p, +8 slots 8/10 %p %p, +0xC slots 3/4 %p %p)", g_vtable[4],
               g_vtable8[8], g_vtable8[10], g_vtableC[3], g_vtableC[4]);
}

// The constructor (detour of 0x005D00F0; `mostDerived` is 1 at both of the
// game's sites). In the game's order: as the most derived object the model
// part's virtual base table (+0x40); the occupant base constructed at its
// address on the object + 8; the model part constructed at its address on
// the object + 0x3C with 0 (it finds its virtual base through the table);
// our vtables, the virtual base's through the table, and the displacement
// word before the base (the table's offset less 0x15C: zero); then the
// fields. The game first fills the transform, the boxes and the vector at
// +0x108 with the float at 0x00B22F98 and overwrites all but the vector;
// only the final values are stored here (the vector's read as the game
// does). Three list sentinels come from the game's allocator in the game's
// order (+0x128 and +0x12C of 0xC bytes, +0x194 of 0x10), each linked to
// itself. Last the occupant type and the model part's dword +0x14 (5).
// Left as the pool's memory holds them, as in the game: +0xD8, +0xDA,
// +0xDB, +0x101 to +0x103, +0x124 to +0x127, +0x181 to +0x183. The abstract
// vtables the game stores first (0x00AA1F84, 0x00A81174, the virtual
// base's 0x00AA1F38) are skipped: nothing can see them.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    if (mostDerived)
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4ConstructionOccupant_vbtable;
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    CallThis<void*, uint32_t>(addr::SC4Model_ConstructModelPart, self + field::kModelPart, 0u);
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kMessageTarget) = g_vtable4;
    At<void**>(self, field::kOccupant) = g_vtable8;
    At<void**>(self, field::kSerializable) = g_vtableC;
    At<void**>(self, field::kHolder) = g_vtable10;
    At<void**>(self, field::kExemplarPart) = g_vtable24;
    At<void**>(self, field::kModelPart) = g_vtable3C;
    const int32_t baseOffset = At<int32_t>(At<char*>(self, field::kVbtable), 0x4);
    At<void**>(self + baseOffset, field::kVbtable) = g_vtable19C;
    At<int32_t>(self + baseOffset, field::kModelPart) = baseOffset - 0x15C;

    constexpr uint32_t kOne = 0x3F800000u;
    At<void*>(self, field::kLot) = nullptr;
    At<void*>(self, field::kBuilding) = nullptr;
    At<uint8_t>(self, field::kTransform) = 0;
    At<uint8_t>(self, field::kTransform + 0x1) = 0;
    for (int i = 0; i < 9; ++i)
        At<uint32_t>(self, field::kTransform + 0x4 + 4 * i) = i % 4 == 0 ? kOne : 0u;
    for (int i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kTransform + 0x28 + 4 * i) = 0;
    At<uint32_t>(self, field::kTransform + 0x34) = kOne;
    for (const std::size_t box : {field::kBox, field::kInnerBox}) {
        for (int i = 0; i < 3; ++i) {
            At<uint32_t>(self, box + 4 * i) = kOne;
            At<uint32_t>(self, box + 0xC + 4 * i) = 0;
        }
    }
    At<uint8_t>(self, field::kLoaded) = 0;
    At<uint32_t>(self, field::kBuffer6) = 0;
    At<uint32_t>(self, field::kBuffer6 + 0x4) = 0;
    At<uint32_t>(self, field::kBuffer6 + 0x8) = 0;
    At<uint8_t>(self, 0x100) = 0;
    At<uint32_t>(self, field::kCrane) = 0;
    for (int i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kCranePlace + 4 * i) = Global<uint32_t>(addr::cSC4ConstructionOccupant_DefaultFloat);
    At<uint32_t>(self, 0x114) = 0;
    At<uint32_t>(self, field::kRows) = 0;
    At<uint32_t>(self, field::kRows + 0x4) = 0;
    At<uint32_t>(self, field::kRows + 0x8) = 0;
    for (const std::size_t list : {field::kProps, field::kStageProps}) {
        At<void*>(self, list) = nullptr;
        void** const sentinel = CallCdecl<void**, std::size_t>(addr::SC4_stl_allocate, 0xC);
        sentinel[0] = sentinel;
        sentinel[1] = sentinel;
        At<void**>(self, list) = sentinel;
    }
    for (std::size_t offset = field::kEffects; offset < field::kCraneBusy; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kCraneBusy) = 0;
    for (std::size_t offset = field::kCraneTime; offset <= field::kNetwork; offset += 4)
        At<uint32_t>(self, offset) = 0;
    void** const sentinel = CallCdecl<void**, std::size_t>(addr::SC4_stl_allocate, 0x10);
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    At<void**>(self, field::kNetwork) = sentinel;
    At<uint32_t>(self, field::kType) = kOccupantType;
    for (std::size_t offset = field::kBuffers; offset < field::kBuffer6; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kModelKind) = 5;
    static int created = 0;
    if (created++ < 3)
        log::Write("constrocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x005CA8A0; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x005D0BC0): an item of the pool, constructed as
// the most derived object; the result is the object itself.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return Construct(static_cast<char*>(PoolAllocate()), nullptr, 1u);
}

using ConstructFn = void*(__fastcall*)(char*, void*, uint32_t);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer by the COM, for StartConstruction and for a save's
// reader; its own call of the constructor at 0x005D0BEE returns EAX as it
// is) and the lot developer's StartConstruction when the COM gave none,
// 0x006CC7F5 (the allocator; add esp, test eax after) and 0x006CC805 (the
// constructor; EAX stored and used as the object after); our own
// StartConstruction (src/core/lot_construct.cpp) calls both at their
// addresses. None keeps anything in ECX or EDX across the calls. The
// deleting destructor is reached through the holder part's slot 22 and the
// model part's slot 0 only (0x005D04F0 and 0x005D0510 jump to it), so the
// vtable copies replace it without a detour; an occupant made before this
// group is installed (there are none at PostAppInit) would keep the game's.
// The pool's own page functions (0x005CA860, 0x005CD210, in the pool's
// vtables 0x00AA1F98 and 0x00AA20A0) stay the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::SC4ConstructionOccupant_Construct);
    const addr::Entry* const allocate = addr_check::Find(addr::SC4ConstructionOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4ConstructionOccupant_Create);
    return construct && allocate && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.constrocc.object",
    "simulators/constrocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4ConstructionOccupant_Construct,
    "Our cSC4ConstructionOccupant object: the constructor, the factory and the pool allocation (detours), the "
    "destructor, identity; methods from copies of the (patched) game vtables taken once "
    "(src/core/constrocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::constrocc_sim
