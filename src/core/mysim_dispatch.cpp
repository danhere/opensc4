// cSC4MySimDispatch (docs/mysim-agents.md): the object that takes a
// MySim from place to place (cSC4MySim+0x20C). Replacements, each off by
// default:
//
//   simulators.mysim_agents.dispatch_state: the state, flags and status
//     setters, the events they post to the MySim, the vehicle, the model
//     lookups, the route reset and the vtable's arrived and abort slots;
//   simulators.mysim_agents.place: SC4MySimPlace, a place a MySim is at or
//     goes to (0x28 bytes: occupant +0, position +4/+8, cell +0xC/+0x10,
//     neighbour connection +0x14/+0x18, in the city +0x1C, work +0x1D);
//   simulators.mysim_agents.dispatch_trip: starting a trip (the places to
//     go, a deque at +0x160), home, destination, model keys, Clear, Reset
//     and Shutdown;
//   simulators.mysim_agents.dispatch_route: the route (a deque of 3-byte
//     cells at +0x128: x, z, kind; the segment in hand is +0x30 to +0x2C),
//     snapping a place to a road, and a destination outside the city;
//   simulators.mysim_agents.dispatch_vehicle: making and launching the
//     vehicle (or walker) for a segment, and the location marker;
//   simulators.mysim_agents.dispatch_legs: GoTo and the vtable's next-leg
//     slot (home to work and back);
//   simulators.mysim_agents.dispatch_step: Step, the trip state machine
//     (stopped: the next place; travelling: the route and the vehicle;
//     none: back home);
//   simulators.mysim_agents.dispatch_segment: EndSegment, what happens when
//     a segment ends (to and from the car, transit stops and vehicles,
//     arriving);
//   simulators.mysim_agents.dispatch_save: Write and Read (the
//     cIGZSerializable part at +0xC).
//
// The deque and map helpers it uses stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstdint>

namespace opensc4::mysim_dispatch {
namespace {

namespace field {
constexpr std::size_t kChanges = 0x34;  // counts every change
constexpr std::size_t kMarker = 0x3C;   // overlay marker id, -1 none
constexpr std::size_t kState = 0x40;    // -1 none, 3 stopped
constexpr std::size_t kPrevious = 0x44;
constexpr std::size_t kFlags = 0x48;
constexpr std::size_t kNow = 0x4C;
constexpr std::size_t kStatus = 0x50;   // 0xFF none, 0xFE stopped
constexpr std::size_t kWaitStart = 0x54;
constexpr std::size_t kHere = 0x5C;     // SC4MySimPlace, 0x28 bytes each
constexpr std::size_t kHome = 0x84;
constexpr std::size_t kTarget = 0xAC;
constexpr std::size_t kWork = 0xD4;
constexpr std::size_t kShown = 0xFC;
constexpr std::size_t kVehicle = 0x118;
constexpr std::size_t kOwner = 0x11C;   // the cSC4MySim
constexpr std::size_t kRouteView = 0x124;
constexpr std::size_t kRoute = 0x128;   // deque of cells
constexpr std::size_t kModels = 0x150;  // map: kind -> model key
constexpr std::size_t kPlaces = 0x160;  // deque of places still to go
} // namespace field

constexpr uint32_t kEventFlags = 0x4BB2D020;
constexpr uint32_t kEventState = 0x4BB2CFC0;
constexpr uint32_t kIidAutomaton = 0xA9B40F05;

using ThisFn = void(__thiscall*)(void* self);

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

void Assign(char* self, std::size_t at, const char* from)
{
    reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4MySimPlace_Assign)(self + at, from);
}

void __fastcall PostEvent(char* self, void*, uint32_t id, uint32_t a, uint32_t b, uint32_t data)
{
    char* const owner = At<char*>(self, field::kOwner);
    if (!owner)
        return;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory)
                                 : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, uint32_t{0x2BA06B99});
    Virtual<void>(message, 0x2C, id);
    Virtual<void>(message, 0x40, a);
    Virtual<void>(message, 0x54, b);
    Virtual<void>(message, 0x68, data);
    Virtual<void>(owner + 0x10, 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

void Post(char* self, uint32_t id, uint32_t a, uint32_t b)
{
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(
        addr::cSC4MySimDispatch_PostEvent)(self, id, a, b, 0);
}

// The flags changed to `value`, posted and counted when they differ.
void ChangeFlags(char* self, uint32_t value)
{
    const uint32_t old = At<uint32_t>(self, field::kFlags);
    At<uint32_t>(self, field::kFlags) = value;
    if (old != value) {
        Post(self, kEventFlags, old, value);
        ++At<int32_t>(self, field::kChanges);
    }
}

// The state change, if any, posted and counted.
void PostStateChange(char* self)
{
    const int32_t previous = At<int32_t>(self, field::kPrevious);
    const int32_t state = At<int32_t>(self, field::kState);
    if (previous != state) {
        Post(self, kEventState, static_cast<uint32_t>(previous), static_cast<uint32_t>(state));
        ++At<int32_t>(self, field::kChanges);
    }
}

uint8_t __fastcall SetStatus(char* self, void*, uint32_t status)
{
    const uint32_t old = At<uint32_t>(self, field::kStatus);
    At<uint32_t>(self, field::kStatus) = status;
    if (status != old)
        ++At<int32_t>(self, field::kChanges);
    return 1;
}

int32_t __fastcall WaitLength(char* self, void*)
{
    if (!((At<uint32_t>(self, field::kFlags) >> 4) & 1) &&
        At<int32_t>(self, field::kNow) - At<int32_t>(self, field::kWaitStart) > 0xF)
        At<int32_t>(self, field::kWaitStart) = 0;
    return At<int32_t>(self, field::kWaitStart);
}

// The game calls these two expecting registers kept that the calling
// convention lets a callee change: Step (0x0043699A) relies on ECX after
// SetStatus, MakeVehicle (0x00435226) on EDX after ModelService. The
// thunks keep both.
__declspec(naked) void SetStatusThunk()
{
    __asm push ecx
    __asm push edx
    __asm push dword ptr [esp + 12]
    __asm call SetStatus
    __asm pop edx
    __asm pop ecx
    __asm ret 4
}

uint8_t __fastcall SetFlags(char* self, void*, uint32_t mask, uint32_t set)
{
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    ChangeFlags(self, static_cast<uint8_t>(set) ? flags | mask : flags & ~mask);
    return 1;
}

uint8_t __fastcall SetState(char* self, void*, int32_t state)
{
    At<int32_t>(self, field::kPrevious) = At<int32_t>(self, field::kState);
    At<int32_t>(self, field::kState) = state;
    if (state == 3)
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 2);
    PostStateChange(self);
    return 1;
}

// Stopped where it is going: the current place becomes work's, status
// 0xFE, state 3.
void __fastcall Stop(char* self, void*)
{
    Assign(self, field::kHere, self + field::kWork);
    if (At<uint32_t>(self, field::kStatus) != 0xFE)
        ++At<int32_t>(self, field::kChanges);
    const int32_t state = At<int32_t>(self, field::kState);
    ++At<int32_t>(self, field::kChanges);
    At<uint32_t>(self, field::kStatus) = 0xFE;
    At<int32_t>(self, 0x80) = At<int32_t>(self, 0x7C) + At<int32_t>(self, field::kNow);
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    At<int32_t>(self, field::kPrevious) = state;
    At<int32_t>(self, field::kState) = 3;
    ChangeFlags(self, flags | 2);
    PostStateChange(self);
}

uint8_t __fastcall HideLocation(char* self, void*)
{
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFFEFF);
    if (At<int32_t>(self, field::kMarker) != -1) {
        if (void* const service = Global<void*>(addr::SC4_DispatchService)) {
            void* const overlays = Virtual<void*>(service, 0x58);
            reinterpret_cast<void(__thiscall*)(void*, int32_t, uint32_t)>(addr::SC4OverlayManager_Remove)(
                overlays, At<int32_t>(self, field::kMarker), 0);
        }
    }
    At<int32_t>(self, field::kMarker) = -1;
    if (void* const shown = At<void*>(self, field::kShown)) {
        At<void*>(self, field::kShown) = nullptr;
        Release(shown);
    }
    return 1;
}

void __fastcall Reshow(char* self, void*)
{
    const bool shown = (At<uint32_t>(self, field::kFlags) >> 8) & 1;
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_HideLocation)(self);
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    ChangeFlags(self, shown ? flags | 0x100 : flags & 0xFFFFFEFF);
}

uint8_t __fastcall SetVehicle(char* self, void*, void* vehicle)
{
    if (At<void*>(self, field::kShown) != vehicle)
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Reshow)(self);
    void* automaton = nullptr;
    if (void* const old = At<void*>(self, field::kVehicle)) {
        if (Virtual<uint8_t>(old, 0x0, kIidAutomaton, &automaton) && Virtual<void*>(automaton, 0x14) != vehicle) {
            Virtual<void>(automaton, 0xA0, self + 8);
            Virtual<void>(automaton, 0x34, uint32_t{0});
        }
    }
    void* const old = At<void*>(self, field::kVehicle);
    if (vehicle != old) {
        if (vehicle)
            Virtual<uint32_t>(vehicle, 0x4);
        At<void*>(self, field::kVehicle) = vehicle;
        Release(old);
    }
    ++At<int32_t>(self, field::kChanges);
    Release(automaton);
    return 1;
}

void __fastcall Finish(char* self, void*)
{
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    const uint32_t status = At<uint32_t>(self, field::kStatus);
    if (((flags & 0x12) || status == 1 || status == 0) && !(At<uint8_t>(self, field::kFlags) & 1))
        ChangeFlags(self, flags | 0x2401);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x440);
    Post(self, 0xEBA06C12, 0, 0);
}

void* __fastcall Mover(char* self, void*)
{
    void* mover = nullptr;
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    if (((flags >> 2) & 1) || !((flags >> 8) & 1))
        return nullptr;
    if (void* const vehicle = At<void*>(self, field::kVehicle))
        mover = vehicle;
    if (At<int32_t>(self, field::kState) != 3 && !((flags >> 4) & 1))
        return mover;
    void* const here = At<void*>(self, field::kHere);
    if (!here)
        return mover;
    void* building = nullptr;
    if (!Virtual<uint8_t>(here, 0x0, uint32_t{0xC9B6D7D6}, &building))
        mover = At<void*>(self, field::kHere);
    Release(building);
    return mover;
}

uint8_t __fastcall VehicleAutomaton(char* self, void*, void** out)
{
    *out = nullptr;
    void* const vehicle = At<void*>(self, field::kVehicle);
    return vehicle && Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, out) ? 1 : 0;
}

void* __fastcall ModelService(void*, void*, uint32_t model)
{
    switch (model) {
    case 0x4107:
    case 0x4149:
    case 0x414A:
        return Global<void*>(addr::SC4_Service43D48);
    case 0x4200:
    case 0x420B:
    case 0x4215:
    case 0x4217:
    case 0x422C:
    case 0x422E:
        return Global<void*>(addr::SC4_Service43D44);
    default:
        return nullptr;
    }
}

__declspec(naked) void ModelServiceThunk()
{
    __asm push ecx
    __asm push edx
    __asm push dword ptr [esp + 12]
    __asm call ModelService
    __asm pop edx
    __asm pop ecx
    __asm ret 4
}

struct MapNode {
    uint8_t color;
    uint8_t pad[3];
    MapNode* parent;
    MapNode* left;
    MapNode* right;
    int32_t kind;
    uint32_t key[3];
    int32_t index;
};

MapNode* FindModel(char* self, int32_t kind)
{
    MapNode* found;
    reinterpret_cast<void(__thiscall*)(void*, MapNode**, const int32_t*)>(addr::SC4Sint32Map_Find)(
        self + field::kModels, &found, &kind);
    return found == At<MapNode*>(self, field::kModels) ? nullptr : found;
}

uint32_t* __fastcall ModelKey(char* self, void*, uint32_t* out, int32_t kind)
{
    if (const MapNode* const found = FindModel(self, kind)) {
        out[0] = found->key[0];
        out[1] = found->key[1];
        out[2] = found->key[2];
    } else {
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
    }
    return out;
}

int32_t __fastcall ModelIndex(char* self, void*, int32_t kind)
{
    const MapNode* const found = FindModel(self, kind);
    return found ? found->index : -1;
}

void __fastcall ModelFor(char* self, void*, uint32_t status, uint32_t* model, int32_t* kind)
{
    uint32_t id = 0;
    int32_t which = -1;
    switch (status) {
    case 0:
    case 0xFE: {
        id = 0x4107;
        which = (At<uint8_t>(self, field::kFlags) & 1) || status == 0xFE ? 2 : 1;
        uint32_t key[3];
        reinterpret_cast<uint32_t*(__thiscall*)(void*, uint32_t*, int32_t)>(addr::cSC4MySimDispatch_ModelKey)(
            self, key, which);
        if (!key[0] && !key[1] && !key[2]) {
            char* const owner = At<char*>(self, field::kOwner);
            id = Virtual<uint8_t>(owner + 0xB8, 0x48) ? 0x4149 : 0x414A;
        }
        break;
    }
    case 1:
        id = 0x420B;
        which = 0;
        break;
    case 2:
        id = 0x4200;
        break;
    case 3:
        id = 0x4215;
        break;
    case 6:
        id = 0x4217;
        break;
    case 7:
        id = 0x422C;
        break;
    case 8:
        id = 0x422E;
        break;
    default:
        break;
    }
    if (kind)
        *kind = which;
    if (model)
        *model = id;
}

void __fastcall ClearRoute(char* self, void*)
{
    reinterpret_cast<ThisFn>(addr::SC4CellDeque_Clear)(self + field::kRoute);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFA7FF);
    At<int32_t>(self, 0x30) = 0;
    At<int32_t>(self, 0x2C) = 0;
    if (void* const view = At<void*>(self, field::kRouteView)) {
        Virtual<void>(view, 0x58);
        Virtual<void>(At<void*>(self, field::kRouteView), 0x3C);
        Virtual<void>(At<void*>(self, field::kRouteView), 0x20, self + field::kRoute);
    }
}

uint8_t Same(char* self, std::size_t a, std::size_t b)
{
    return reinterpret_cast<uint8_t(__thiscall*)(const void*, const void*)>(addr::SC4MySimPlace_Same)(self + a,
                                                                                                        self + b);
}

uint8_t __fastcall IsArrived(char* self, void*)
{
    void* automaton = nullptr;
    if (At<int32_t>(self, field::kState) == 2) {
        if (void* const vehicle = At<void*>(self, field::kVehicle)) {
            if (Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton) && At<uint32_t>(self, field::kStatus) == 1) {
                Release(automaton);
                return 1;
            }
        }
    }
    uint8_t result = 0;
    if (At<int32_t>(self, field::kState) == 3 &&
        (Same(self, field::kHere, field::kHome) || Same(self, field::kHere, field::kWork)))
        result = 1;
    Release(automaton);
    return result;
}

uint8_t __fastcall Abort(char* self, void*)
{
    if (At<uint8_t>(self, 0x4A) & 1) {
        void* automaton = nullptr;
        if (void* const vehicle = At<void*>(self, field::kVehicle)) {
            if (Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton)) {
                Virtual<void>(automaton, 0xA0, self + 8);
                Virtual<void>(automaton, 0x34, uint32_t{0});
            }
        }
        reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_Clear)(self + field::kPlaces);
        Assign(self, field::kHere, self + field::kHome);
        Assign(self, field::kTarget, self + field::kHome);
        if (At<uint32_t>(self, field::kStatus) != 0xFF)
            ++At<int32_t>(self, field::kChanges);
        const int32_t state = At<int32_t>(self, field::kState);
        At<uint32_t>(self, field::kStatus) = 0xFF;
        At<int32_t>(self, field::kPrevious) = state;
        At<int32_t>(self, field::kState) = -1;
        if (state != -1) {
            Post(self, kEventState, static_cast<uint32_t>(state), 0xFFFFFFFF);
            ++At<int32_t>(self, field::kChanges);
        }
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_ClearRoute)(self);
        if (void* const vehicle = At<void*>(self, field::kVehicle)) {
            At<void*>(self, field::kVehicle) = nullptr;
            Release(vehicle);
        }
        Release(automaton);
    }
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFCFFFF);
    return 1;
}

// --- SC4MySimPlace ---------------------------------------------------------------------

struct Place {
    void* object;
    float x;
    float z;
    int32_t cellX;
    int32_t cellZ;
    int32_t connection[2];
    uint8_t local;
    uint8_t work;
    uint8_t pad[2];
    uint32_t extra[2];
};
static_assert(sizeof(Place) == 0x28);

void HoldObject(Place* place, void* object)
{
    void* const old = place->object;
    if (object == old)
        return;
    if (object)
        Virtual<uint32_t>(object, 0x4);
    place->object = object;
    Release(old);
}

void __cdecl ToCell(float x, float z, int32_t* cellX, int32_t* cellZ)
{
    *cellX = static_cast<int32_t>(std::floor(static_cast<double>(x) * 0.0625));
    *cellZ = static_cast<int32_t>(std::floor(static_cast<double>(z) * 0.0625));
}

void CallToCell(float x, float z, int32_t* cellX, int32_t* cellZ)
{
    reinterpret_cast<void(__cdecl*)(float, float, int32_t*, int32_t*)>(addr::SC4MySimPlace_ToCell)(x, z, cellX, cellZ);
}

Place* __fastcall Construct(Place* place, void*)
{
    place->object = nullptr;
    place->cellX = -1;
    place->cellZ = -1;
    place->connection[0] = -1;
    place->connection[1] = -1;
    place->work = 0;
    place->extra[0] = 0;
    place->extra[1] = 0;
    place->local = 1;
    place->x = static_cast<float>(place->cellX << 4);
    place->z = static_cast<float>(place->cellZ << 4);
    return place;
}

void __fastcall SetCell(Place* place, void*, int32_t cellX, int32_t cellZ)
{
    place->cellX = cellX;
    place->cellZ = cellZ;
    place->x = static_cast<float>(cellX << 4);
    place->z = static_cast<float>(cellZ << 4);
}

void __fastcall SetPosition(Place* place, void*, float x, float z)
{
    place->x = x;
    place->z = z;
    CallToCell(x, z, &place->cellX, &place->cellZ);
}

uint8_t __fastcall SetOccupant(Place* place, void*, void* occupant)
{
    Place empty;
    Construct(&empty, nullptr);
    reinterpret_cast<void(__thiscall*)(Place*, const Place*)>(addr::SC4MySimPlace_Assign)(place, &empty);
    HoldObject(place, occupant);
    place->local = 1;
    if (place->object) {
        float position[3];
        Virtual<void>(place->object, 0x20, position);
        place->x = position[0];
        place->z = position[2];
        CallToCell(position[0], position[2], &place->cellX, &place->cellZ);
    }
    return 1;
}

int32_t __fastcall Write(Place* place, void*, void* stream)
{
    reinterpret_cast<void(__cdecl*)(void*, void*)>(addr::SC4Stream_WriteReference)(stream, place->object);
    Virtual<void>(stream, 0x30, place->x);
    Virtual<void>(stream, 0x30, place->z);
    Virtual<void>(stream, 0x20, place->cellX);
    Virtual<void>(stream, 0x20, place->cellZ);
    Virtual<void>(stream, 0x20, place->connection[0]);
    Virtual<void>(stream, 0x20, place->connection[1]);
    using WriteBoolFn = void(__cdecl*)(void*, uint32_t);
    reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool)(stream, place->local);
    reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool)(stream, place->work);
    Virtual<void>(stream, 0x20, place->extra[0]);
    Virtual<void>(stream, 0x20, place->extra[1]);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

int32_t __fastcall Read(Place* place, void*, void* stream)
{
    if (void* const old = place->object) {
        place->object = nullptr;
        Release(old);
    }
    reinterpret_cast<void(__cdecl*)(void*, uint32_t, void*, uint32_t)>(addr::SC4Stream_ReadReference)(
        stream, 0x26BB767E, &place->object, 0);
    Virtual<void>(stream, 0x30, &place->x);
    Virtual<void>(stream, 0x30, &place->z);
    Virtual<void>(stream, 0x20, &place->cellX);
    Virtual<void>(stream, 0x20, &place->cellZ);
    Virtual<void>(stream, 0x20, &place->connection[0]);
    Virtual<void>(stream, 0x20, &place->connection[1]);
    using ReadBoolFn = void(__cdecl*)(void*, void*);
    reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool)(stream, &place->local);
    reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool)(stream, &place->work);
    Virtual<void>(stream, 0x20, &place->extra[0]);
    Virtual<void>(stream, 0x20, &place->extra[1]);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

bool SamePlace(const Place* a, const Place* b)
{
    return a->local == b->local && a->object == b->object && a->work == b->work && a->cellX == b->cellX &&
           a->cellZ == b->cellZ &&
           reinterpret_cast<uint8_t(__cdecl*)(const int32_t*, const int32_t*)>(addr::SC4_PairsEqual)(
               a->connection, b->connection);
}

int32_t __fastcall SameFn(const Place* a, void*, const Place* b)
{
    return SamePlace(a, b) ? 1 : 0;
}

int32_t __fastcall Differ(const Place* a, void*, const Place* b)
{
    return SamePlace(a, b) ? 0 : 1;
}

void __fastcall Reset(Place* place, void*)
{
    place->work = 0;
    place->local = 1;
    if (void* const old = place->object) {
        place->object = nullptr;
        Release(old);
    }
    place->cellX = -1;
    place->cellZ = -1;
    place->x = -16.0f;
    place->z = -16.0f;
}

uint8_t __fastcall IsBuilding(void*, void*, const Place* place)
{
    void* building = nullptr;
    if (!place->object)
        return 0;
    const uint8_t found = Virtual<uint8_t>(place->object, 0x0, uint32_t{0x87DDA3A9}, &building);
    Release(building);
    return found ? 1 : 0;
}

int32_t* __fastcall Rect(void*, void*, int32_t* out, const Place* place)
{
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const lot = place->object ? Virtual<void*>(lots, 0x38, place->object)
                                    : Virtual<void*>(lots, 0x18, place->cellX, place->cellZ, uint32_t{0});
    if (lot) {
        int32_t rect[4];
        Virtual<void>(lot, 0x38, rect);
        out[0] = rect[0];
        out[1] = rect[1];
        out[2] = rect[2];
        out[3] = rect[3];
    } else {
        out[0] = place->cellX;
        out[1] = place->cellZ;
        out[2] = place->cellX;
        out[3] = place->cellZ;
    }
    return out;
}

int32_t __fastcall Distance(void* self, void*, const Place* a, const Place* b)
{
    using RectFn = int32_t*(__thiscall*)(void*, int32_t*, const Place*);
    int32_t ra[4];
    int32_t rb[4];
    reinterpret_cast<RectFn>(addr::SC4MySimPlace_Rect)(self, ra, a);
    reinterpret_cast<RectFn>(addr::SC4MySimPlace_Rect)(self, rb, b);
    const float a0[2] = {static_cast<float>(ra[0]), static_cast<float>(ra[1])};
    const float a1[2] = {static_cast<float>(ra[2]), static_cast<float>(ra[3])};
    const float corners[4][2] = {
        {static_cast<float>(rb[0]), static_cast<float>(rb[1])},
        {static_cast<float>(rb[2]), static_cast<float>(rb[3])},
        {static_cast<float>(rb[2]), static_cast<float>(rb[1])},
        {static_cast<float>(rb[0]), static_cast<float>(rb[3])},
    };
    using BoxFn = float(__cdecl*)(const float*, const float*, const float*);
    const auto box = reinterpret_cast<BoxFn>(addr::SC4_BoxDistance);
    const float d1 = box(corners[0], a0, a1);
    const float d2 = box(corners[1], a0, a1);
    const float d3 = box(corners[2], a0, a1);
    const float d4 = box(corners[3], a0, a1);
    const float m34 = d4 < d3 ? d4 : d3;
    const float m12 = d2 < d1 ? d2 : d1;
    return static_cast<int32_t>(m34 < m12 ? m34 : m12);
}

void __fastcall Normalize(int32_t* rect, void*)
{
    if (rect[0] > rect[2]) {
        const int32_t t = rect[0];
        rect[0] = rect[2];
        rect[2] = t;
    }
    if (rect[1] > rect[3]) {
        const int32_t t = rect[1];
        rect[1] = rect[3];
        rect[3] = t;
    }
}

Place* __fastcall Copy(Place* place, void*, const Place* from)
{
    place->object = from->object;
    if (place->object)
        Virtual<uint32_t>(place->object, 0x4);
    place->x = from->x;
    place->z = from->z;
    place->cellX = from->cellX;
    place->cellZ = from->cellZ;
    place->connection[0] = from->connection[0];
    place->connection[1] = from->connection[1];
    place->local = from->local;
    place->work = from->work;
    place->extra[0] = from->extra[0];
    place->extra[1] = from->extra[1];
    return place;
}

Place* __fastcall AssignPlace(Place* place, void*, const Place* from)
{
    if (from != place)
        HoldObject(place, from->object);
    place->x = from->x;
    place->z = from->z;
    place->cellX = from->cellX;
    place->cellZ = from->cellZ;
    place->connection[0] = from->connection[0];
    place->connection[1] = from->connection[1];
    place->local = from->local;
    place->work = from->work;
    place->extra[0] = from->extra[0];
    place->extra[1] = from->extra[1];
    return place;
}

uint8_t __fastcall IsValid(Place* place, void*)
{
    if (!place->local)
        return reinterpret_cast<uint8_t(__thiscall*)(Place*, int32_t, int32_t)>(addr::SC4MySimPlace_ValidConnection)(
            place, place->connection[0], place->connection[1]);
    if (place->cellX < 0 || place->cellZ < 0)
        return 0;
    void* const city = Global<void*>(addr::SC4_City);
    if (place->cellX >= Virtual<int32_t>(city, 0x174))
        return 0;
    return place->cellZ < Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178) ? 1 : 0;
}

bool InstallPlace(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4MySimPlace_ToCell, reinterpret_cast<void*>(&ToCell)},
        {addr::SC4MySimPlace_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4MySimPlace_Clear, reinterpret_cast<void*>(&SetCell)},
        {addr::SC4MySimPlace_SetPosition, reinterpret_cast<void*>(&SetPosition)},
        {addr::SC4MySimPlace_SetOccupant, reinterpret_cast<void*>(&SetOccupant)},
        {addr::SC4MySimPlace_Write, reinterpret_cast<void*>(&Write)},
        {addr::SC4MySimPlace_Read, reinterpret_cast<void*>(&Read)},
        {addr::SC4MySimPlace_Same, reinterpret_cast<void*>(&SameFn)},
        {addr::SC4MySimPlace_Differ, reinterpret_cast<void*>(&Differ)},
        {addr::SC4MySimPlace_Reset, reinterpret_cast<void*>(&Reset)},
        {addr::SC4MySimPlace_IsBuilding, reinterpret_cast<void*>(&IsBuilding)},
        {addr::SC4MySimPlace_Rect, reinterpret_cast<void*>(&Rect)},
        {addr::SC4MySimPlace_Distance, reinterpret_cast<void*>(&Distance)},
        {addr::SC4Rect_Normalize, reinterpret_cast<void*>(&Normalize)},
        {addr::SC4MySimPlace_Copy, reinterpret_cast<void*>(&Copy)},
        {addr::SC4MySimPlace_Assign, reinterpret_cast<void*>(&AssignPlace)},
        {addr::SC4MySimPlace_IsValid, reinterpret_cast<void*>(&IsValid)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_placeRegistered = registry::Register({
    "simulators.mysim_agents.place",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4MySimPlace_Construct,
    "Our SC4MySimPlace: places a MySim is at or goes to (docs/mysim-agents.md)",
    false,
    InstallPlace,
});

// --- Trips ------------------------------------------------------------------------------

struct PlaceDeque { // std::deque<SC4MySimPlace>, 3 per block
    char* firstCur;
    char* firstBegin;
    char* firstEnd;
    char** firstNode;
    char* lastCur;
    char* lastBegin;
    char* lastEnd;
    char** lastNode;
    char** map;
    uint32_t mapSize;
};

void CopyPlace(char* to, const void* from)
{
    reinterpret_cast<void*(__thiscall*)(void*, const void*)>(addr::SC4MySimPlace_Copy)(to, from);
}

// The place to go next (flags 1: first, else last; 4: the others dropped).
uint8_t __fastcall StartTrip(char* self, void*, const void* place, uint32_t flags)
{
    if (flags & 4)
        reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_Clear)(self + field::kPlaces);
    auto* const places = reinterpret_cast<PlaceDeque*>(self + field::kPlaces);
    using PushFn = void(__thiscall*)(void*, const void*);
    if (flags & 1) {
        if (places->firstCur != places->firstBegin) {
            if (char* const at = places->firstCur - 0x28)
                CopyPlace(at, place);
            places->firstCur -= 0x28;
        } else {
            reinterpret_cast<PushFn>(addr::SC4MySimPlaceDeque_PushFrontSlow)(places, place);
        }
    } else if (places->lastCur != places->lastEnd - 0x28) {
        if (places->lastCur)
            CopyPlace(places->lastCur, place);
        places->lastCur += 0x28;
    } else {
        reinterpret_cast<PushFn>(addr::SC4MySimPlaceDeque_PushBackSlow)(places, place);
    }
    return 1;
}

uint8_t __fastcall SetModel(char* self, void*, int32_t kind, const uint32_t* key, uint32_t index)
{
    const uint32_t k0 = key[0];
    const uint32_t k1 = key[1];
    const uint32_t k2 = key[2];
    auto* const value = reinterpret_cast<uint32_t*(__thiscall*)(void*, const int32_t*)>(addr::SC4MySimModelMap_At)(
        self + field::kModels, &kind);
    value[1] = k1;
    value[0] = k0;
    value[2] = k2;
    value[3] = index;
    return 1;
}

uint8_t IsValidPlace(const void* place)
{
    return reinterpret_cast<uint8_t(__thiscall*)(const void*)>(addr::SC4MySimPlace_IsValid)(place);
}

uint8_t __fastcall SetHome(char* self, void*, const Place* home)
{
    uint8_t result = 0;
    if (IsValidPlace(home) && home->local && !home->work) {
        result = 1;
        if (Same(self, field::kHere, field::kHome)) {
            if (void* const object = At<void*>(self, field::kHere)) {
                At<void*>(self, field::kHere) = nullptr;
                Release(object);
            }
        }
        Assign(self, field::kHome, reinterpret_cast<const char*>(home));
        ++At<int32_t>(self, field::kChanges);
    }
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    ChangeFlags(self, result ? flags | 0x80 : flags & 0xFFFFFF7F);
    return result;
}

uint8_t __fastcall SetDestination(char* self, void*, const void* place, uint32_t post)
{
    Place copy;
    CopyPlace(reinterpret_cast<char*>(&copy), place);
    const bool postIt = static_cast<uint8_t>(post) != 0;
    bool ok = IsValidPlace(&copy) != 0;
    if (!ok && postIt) {
        ok = At<int32_t>(self, field::kState) == 2 ||
             reinterpret_cast<uint8_t(__thiscall*)(void*, Place*)>(addr::cSC4MySimDispatch_PlanOutside)(self, &copy);
    }
    if (ok) {
        Assign(self, field::kWork, reinterpret_cast<const char*>(&copy));
        if (postIt)
            Post(self, 0xEBA06BF4, 0, 0);
    }
    Release(copy.object);
    return ok ? 1 : 0;
}

void AssignEmpty(char* self, std::size_t at)
{
    Place empty;
    Construct(&empty, nullptr);
    Assign(self, at, reinterpret_cast<const char*>(&empty));
}

uint8_t __fastcall Clear(char* self, void*)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_HideLocation)(self);
    void* automaton = nullptr;
    if (void* const vehicle = At<void*>(self, field::kVehicle)) {
        if (Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton)) {
            Virtual<void>(automaton, 0xA0, self + 8);
            Virtual<void>(automaton, 0x34, uint32_t{0});
        }
    }
    const uint32_t old = At<uint32_t>(self, field::kFlags);
    At<uint32_t>(self, field::kFlags) = 0;
    if (old) {
        Post(self, kEventFlags, old, 0);
        ++At<int32_t>(self, field::kChanges);
    }
    reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_Clear)(self + field::kPlaces);
    AssignEmpty(self, field::kHere);
    AssignEmpty(self, field::kTarget);
    AssignEmpty(self, field::kHome);
    if (At<uint32_t>(self, field::kStatus) != 0xFF)
        ++At<int32_t>(self, field::kChanges);
    const int32_t state = At<int32_t>(self, field::kState);
    At<uint32_t>(self, field::kStatus) = 0xFF;
    At<int32_t>(self, field::kPrevious) = state;
    At<int32_t>(self, field::kState) = -1;
    if (state != -1) {
        Post(self, kEventState, static_cast<uint32_t>(state), 0xFFFFFFFF);
        ++At<int32_t>(self, field::kChanges);
    }
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_ClearRoute)(self);
    if (void* const vehicle = At<void*>(self, field::kVehicle)) {
        At<void*>(self, field::kVehicle) = nullptr;
        Release(vehicle);
    }
    Release(automaton);
    return 1;
}

constexpr uint32_t kNotifications[] = {0x44778231, 0x8BBBD22A, 0xA6B79601, 0xA6B79611};

uint8_t __fastcall Shutdown(char* self, void*)
{
    if (At<void*>(self, field::kRouteView)) {
        if (void* const frames = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
            Virtual<void>(frames, 0x18, self + 8, uint32_t{0xFF});
        for (const uint32_t type : kNotifications)
            Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x18, self + 8, type);
    }
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Reshow)(self);
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Clear)(self);
    for (const std::size_t at : {std::size_t{0x120}, field::kRouteView})
        if (void* const held = At<void*>(self, at)) {
            At<void*>(self, at) = nullptr;
            Release(held);
        }
    return 1;
}

uint8_t __fastcall ResetDispatch(char* self, void*)
{
    if (!At<void*>(self, field::kRouteView)) {
        for (const uint32_t type : kNotifications)
            Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, self + 8, type);
        if (void* const frames = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
            Virtual<void>(frames, 0x14, self + 8, uint32_t{0xFF});
        if (void* const old = At<void*>(self, field::kRouteView)) {
            At<void*>(self, field::kRouteView) = nullptr;
            Release(old);
        }
        Virtual<void>(Global<void*>(addr::SC4_Service43DA8), 0x18, self + field::kRouteView);
    }
    if (At<uint8_t>(self, 0x188))
        return 1;
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    void* const old = At<void*>(self, 0x120);
    if (clock != old) {
        if (clock)
            Virtual<uint32_t>(clock, 0x4);
        At<void*>(self, 0x120) = clock;
        Release(old);
    }
    AssignEmpty(self, field::kHome);
    AssignEmpty(self, field::kWork);
    Assign(self, field::kHere, self + field::kWork);
    At<uint8_t>(self, field::kWork + 0x1D) = 1;
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    At<int32_t>(self, field::kPrevious) = At<int32_t>(self, field::kState);
    At<int32_t>(self, field::kState) = 3;
    ChangeFlags(self, flags | 2);
    PostStateChange(self);
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_ClearRoute)(self);
    At<uint8_t>(self, 0x10) = 0;
    return 1;
}

bool InstallTrip(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySimDispatch_StartTrip, reinterpret_cast<void*>(&StartTrip)},
        {addr::cSC4MySimDispatch_SetModel, reinterpret_cast<void*>(&SetModel)},
        {addr::cSC4MySimDispatch_SetHome, reinterpret_cast<void*>(&SetHome)},
        {addr::cSC4MySimDispatch_SetDestination, reinterpret_cast<void*>(&SetDestination)},
        {addr::cSC4MySimDispatch_Clear, reinterpret_cast<void*>(&Clear)},
        {addr::cSC4MySimDispatch_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4MySimDispatch_Reset, reinterpret_cast<void*>(&ResetDispatch)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_tripRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_trip",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_StartTrip,
    "Our cSC4MySimDispatch trip start, home, destination, Clear, Reset and Shutdown (docs/mysim-agents.md)",
    false,
    InstallTrip,
});

// --- Route ------------------------------------------------------------------------------

struct CellIterator {
    uint8_t* cur;
    uint8_t* first;
    uint8_t* last;
    uint8_t** node;
};

uint8_t* CellAt(char* self, int32_t index)
{
    CellIterator it = *reinterpret_cast<CellIterator*>(self + field::kRoute);
    reinterpret_cast<void(__thiscall*)(CellIterator*, int32_t)>(addr::SC4CellIterator_Advance)(&it, index);
    return it.cur;
}

int32_t RouteSize(char* self)
{
    return reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::SC4CellDeque_Size)(self + field::kRoute);
}

int32_t& SegmentStart(char* self)
{
    return At<int32_t>(self, 0x30);
}

int32_t& SegmentEnd(char* self)
{
    return At<int32_t>(self, 0x2C);
}

uint8_t __fastcall ReachedStop(char* self, void*)
{
    void* const network = Global<void*>(addr::SC4_Service43D54);
    if (SegmentStart(self) >= SegmentEnd(self))
        return 0;
    do {
        const uint8_t* const cell = CellAt(self, SegmentStart(self));
        if (Virtual<int32_t>(network, 0x20, uint32_t{cell[0]}, uint32_t{cell[1]}, uint32_t{0x1FCF}, uint32_t{0}))
            return 1;
        ++SegmentStart(self);
    } while (SegmentStart(self) < SegmentEnd(self));
    return 0;
}

void __fastcall AdvanceRoute(char* self, void*)
{
    for (;;) {
        SegmentStart(self) = SegmentEnd(self);
        const int32_t size = RouteSize(self);
        const uint32_t kind = CellAt(self, SegmentEnd(self))[2];
        if (size > SegmentEnd(self)) {
            for (;;) {
                const int32_t at = SegmentEnd(self);
                if (CellAt(self, at)[2] != kind)
                    break;
                SegmentEnd(self) = at + 1;
                if (!(size > at + 1))
                    break;
            }
        }
        if (reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_ReachedStop)(self))
            return;
        if (SegmentStart(self) != SegmentEnd(self) || kind == 0xFE || !(SegmentStart(self) < size))
            return;
    }
}

uint8_t __fastcall HasRouteAhead(char* self, void*)
{
    if (SegmentStart(self) >= SegmentEnd(self))
        return 0;
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_AdvanceRoute)(self);
    if (SegmentStart(self) >= SegmentEnd(self))
        return 0;
    return CellAt(self, SegmentStart(self))[2] != 0xFE ? 1 : 0;
}

uint8_t __fastcall CopyRoute(char* self, void*, char* out, uint32_t append)
{
    if (!static_cast<uint8_t>(append))
        reinterpret_cast<ThisFn>(addr::SC4CellDeque_Clear)(out);
    int32_t count = 0;
    for (int32_t at = SegmentStart(self); at < SegmentEnd(self); ++at, ++count) {
        const uint8_t* const cell = CellAt(self, at);
        auto& end = *reinterpret_cast<CellIterator*>(out + 0x10);
        if (end.cur != end.last - 3) {
            if (end.cur) {
                end.cur[0] = cell[0];
                end.cur[1] = cell[1];
                end.cur[2] = cell[2];
            }
            end.cur += 3;
        } else {
            reinterpret_cast<void(__thiscall*)(void*, const uint8_t*)>(addr::SC4RouteDeque_PushBackSlow)(out, cell);
        }
    }
    return count > 0 ? 1 : 0;
}

// The place moved to a road cell of its lot (the route's next cell if it
// is one of them).
uint8_t __fastcall SnapToRoad(char* self, void*, Place* place)
{
    void* const network = Global<void*>(addr::SC4_Service43D54);
    if (Virtual<int32_t>(network, 0x20, place->cellX, place->cellZ, uint32_t{0x849}, uint32_t{0}))
        return 1;
    int32_t rect[4];
    reinterpret_cast<int32_t*(__thiscall*)(void*, int32_t*, const Place*)>(addr::SC4MySimPlace_Rect)(self, rect,
                                                                                                      place);
    struct {
        void** begin;
        void** end;
        void** capacity;
    } roads{};
    using FreeFn = void(__cdecl*)(void*);
    if (!Virtual<uint8_t>(network, 0x64, &roads, rect, uint32_t{1}, uint32_t{1}, uint32_t{0x849})) {
        if (roads.begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(roads.begin);
        return 0;
    }
    void* chosen = nullptr;
    char* const route = self + field::kRoute;
    if (At<char*>(route, 0x10) != At<char*>(route, 0) && SegmentStart(self) <= SegmentEnd(self) &&
        static_cast<uint32_t>(SegmentStart(self)) < static_cast<uint32_t>(RouteSize(self))) {
        const int32_t index = SegmentStart(self);
        using AtFn = uint8_t*(__thiscall*)(void*, int32_t);
        const int32_t x = reinterpret_cast<AtFn>(addr::SC4CellDeque_At)(route, index)[0];
        const int32_t z = reinterpret_cast<AtFn>(addr::SC4CellDeque_At)(route, index)[1];
        const int32_t count = static_cast<int32_t>(roads.end - roads.begin);
        for (int32_t i = 0; i < count; ++i) {
            int32_t cellX;
            int32_t cellZ;
            Virtual<void>(roads.begin[i], 0xEC, &cellX, &cellZ);
            if (cellX == x && cellZ == z) {
                chosen = roads.begin[i];
                break;
            }
        }
    }
    if (!chosen)
        chosen = roads.begin[0];
    int32_t cellX;
    int32_t cellZ;
    Virtual<void>(chosen, 0xEC, &cellX, &cellZ);
    place->cellX = cellX;
    place->x = static_cast<float>(cellX << 4);
    place->cellZ = cellZ;
    place->z = static_cast<float>(cellZ << 4);
    for (void** at = roads.end; at != roads.begin;) {
        --at;
        Virtual<uint32_t>(*at, 0x8);
        roads.end = at;
    }
    if (roads.begin)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(roads.begin);
    return 1;
}

// A way from home to the place found by the route view: a neighbour
// connection outside the city, or the building on the lot it ends at.
uint8_t __fastcall PlanOutside(char* self, void*, Place* place)
{
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_ClearRoute)(self);
    Place empty;
    Construct(&empty, nullptr);
    reinterpret_cast<void(__thiscall*)(Place*, const Place*)>(addr::SC4MySimPlace_Assign)(place, &empty);
    place->work = 1;
    if (!((At<uint32_t>(self, field::kFlags) >> 7) & 1))
        return 0;
    void* const view = At<void*>(self, field::kRouteView);
    Virtual<void>(view, 0x14, uint32_t{1});
    Virtual<void>(At<void*>(self, field::kRouteView), 0x14, uint32_t{0x10});
    Virtual<void>(At<void*>(self, field::kRouteView), 0x24, uint32_t{0});
    char* const owner = At<char*>(self, field::kOwner);
    uint32_t mode = 1;
    switch (Virtual<int32_t>(owner + 0xB8, 0x4C)) {
    case 2:
        mode = 2;
        break;
    case 3:
        mode = 4;
        break;
    default:
        break;
    }
    Virtual<void>(At<void*>(self, field::kRouteView), 0x28, mode);
    Virtual<void>(At<void*>(self, field::kRouteView), 0x48, uint32_t{1});
    struct Rect {
        int32_t v[4];
    } home;
    reinterpret_cast<int32_t*(__thiscall*)(void*, int32_t*, const void*)>(addr::SC4MySimPlace_Rect)(
        self, home.v, self + field::kHome);
    Virtual<void>(At<void*>(self, field::kRouteView), 0x4C, home);
    Virtual<void>(At<void*>(self, field::kRouteView), 0x34, uint32_t{0x1CF}, uint32_t{1});
    if (Virtual<int32_t>(At<void*>(self, field::kRouteView), 0x5C) != 1)
        return 0;
    int32_t x;
    int32_t z;
    Virtual<void>(At<void*>(self, field::kRouteView), 0x6C, &x, &z);
    void* const city = Global<void*>(addr::SC4_City);
    if (!Virtual<uint8_t>(city, 0x18C, x, z)) {
        void* const connection = Virtual<void*>(Global<void*>(addr::SC4_MonthEndService), 0x30, x, z);
        if (!connection)
            return 0;
        if (x < 0)
            x = 0;
        if (z < 0)
            z = 0;
        if (x >= Virtual<int32_t>(city, 0x174))
            x = Virtual<int32_t>(city, 0x174) - 1;
        if (z >= Virtual<int32_t>(city, 0x178))
            z = Virtual<int32_t>(city, 0x178) - 1;
        place->local = 0;
        if (void* const old = place->object) {
            place->object = nullptr;
            Release(old);
        }
        reinterpret_cast<void(__thiscall*)(Place*, int32_t, int32_t)>(addr::SC4MySimPlace_Clear)(place, x, z);
        Virtual<void>(connection, 0x14, &place->connection[0], &place->connection[1]);
        return 1;
    }
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
    if (!lot)
        return 0;
    void* const building = Virtual<void*>(lot, 0xC0);
    if (!building)
        return 0;
    Place before;
    CopyPlace(reinterpret_cast<char*>(&before), place);
    reinterpret_cast<uint8_t(__thiscall*)(Place*, void*)>(addr::SC4MySimPlace_SetOccupant)(
        place, Virtual<void*>(building, 0xC));
    place->work = before.work;
    place->local = 1;
    Release(before.object);
    return 1;
}

bool InstallRoute(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySimDispatch_ReachedStop, reinterpret_cast<void*>(&ReachedStop)},
        {addr::cSC4MySimDispatch_AdvanceRoute, reinterpret_cast<void*>(&AdvanceRoute)},
        {addr::cSC4MySimDispatch_HasRouteAhead, reinterpret_cast<void*>(&HasRouteAhead)},
        {addr::cSC4MySimDispatch_CopyRoute, reinterpret_cast<void*>(&CopyRoute)},
        {addr::cSC4MySimDispatch_SnapToRoad, reinterpret_cast<void*>(&SnapToRoad)},
        {addr::cSC4MySimDispatch_PlanOutside, reinterpret_cast<void*>(&PlanOutside)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_routeRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_route",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_AdvanceRoute,
    "Our cSC4MySimDispatch route segments, road snapping and destinations outside the city (docs/mysim-agents.md)",
    false,
    InstallRoute,
});

// --- Vehicle ----------------------------------------------------------------------------

uint8_t __fastcall MakeVehicle(char* self, void*, uint32_t status, void** out, void*, void*)
{
    uint32_t model = 0;
    int32_t kind = -1;
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t*, int32_t*)>(addr::cSC4MySimDispatch_ModelFor)(
        self, status, &model, &kind);
    uint32_t key[3] = {0, 0, 0};
    if (const MapNode* const found = FindModel(self, kind)) {
        key[0] = found->key[0];
        key[1] = found->key[1];
        key[2] = found->key[2];
    }
    float position[3] = {At<float>(self, 0x60), 0.0f, At<float>(self, 0x64)};
    if (!IsValidPlace(self + field::kHere)) {
        position[0] = 0.0f;
        position[2] = 0.0f;
    }
    void* const service = reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::cSC4MySimDispatch_ModelService)(
        self, model);
    void* const made = Virtual<void*>(service, 0x3C, model, position, uint32_t{0x214042}, key, self + 8,
                                      uint32_t{0xFF}, uint32_t{0});
    void* held = nullptr;
    if (made) {
        Virtual<uint32_t>(made, 0x4);
        *out = made;
        Virtual<uint32_t>(made, 0x4);
        held = made;
        void* const exemplar = Virtual<void*>(Virtual<void*>(made, 0x14), 0x18);
        reinterpret_cast<void(__cdecl*)(void*, uint32_t, uint32_t)>(addr::SC4_SetPropertyBool)(exemplar, 0x1BC42C49, 1);
    }
    const uint8_t result = *out != nullptr ? 1 : 0;
    Release(held);
    return result;
}

uint32_t __fastcall Launch(char* self, void*, char* route, void* vehicle, uint32_t walking)
{
    if (At<char*>(route, 0x10) == At<char*>(route, 0))
        return 0xFF;
    const bool walk = static_cast<uint8_t>(walking) != 0;
    uint32_t result = 0xFF;
    bool ok = true;
    uint32_t model = 0;
    int32_t kind = -1;
    void* mover = vehicle;
    if (mover)
        Virtual<uint32_t>(mover, 0x4);
    const uint32_t status =
        reinterpret_cast<uint8_t*(__thiscall*)(void*, int32_t)>(addr::SC4CellDeque_At)(route, 0)[2];
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t*, int32_t*)>(addr::cSC4MySimDispatch_ModelFor)(
        self, status, &model, &kind);
    bool make = true;
    if (mover) {
        if (Virtual<uint8_t>(Virtual<void*>(mover, 0x14), 0x58, model) && !Virtual<uint8_t>(mover, 0x7C, uint32_t{1})) {
            make = false;
        } else {
            void* const old = mover;
            mover = nullptr;
            Release(old);
        }
    }
    if (make) {
        ok = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**, uint32_t*, int32_t*)>(
                 addr::cSC4MySimDispatch_MakeVehicle)(self, status, &mover, &model, &kind) != 0;
    }
    if (!mover)
        return result;
    if (ok) {
        Virtual<void>(mover, 0x78, uint32_t{0x10000}, uint32_t{1});
        Virtual<void>(mover, 0x78, uint32_t{0x40}, uint32_t{1});
        Virtual<void>(mover, 0x78, uint32_t{0x200000}, uint32_t{1});
        Virtual<void>(mover, 0x78, uint32_t{0x1000000}, uint32_t{1});
        Virtual<void>(mover, 0x78, uint32_t{0x200}, uint32_t{0});
        Virtual<void>(mover, 0x78, uint32_t{0x2000}, uint32_t{0});
        Virtual<void>(mover, 0x78, uint32_t{0x100}, static_cast<uint32_t>(static_cast<uint8_t>(walking)));
        Virtual<void>(mover, 0x78, uint32_t{0x40}, uint32_t{walk ? 0u : 1u});
        Virtual<void>(mover, 0x78, uint32_t{0x20}, uint32_t{walk ? 0u : 1u});
        Virtual<void>(mover, 0x100, uint32_t{0xFF});
        void* path = nullptr;
        const int32_t index = reinterpret_cast<int32_t(__thiscall*)(void*, int32_t)>(
            addr::cSC4MySimDispatch_ModelIndex)(self, kind);
        bool launched = false;
        if (Virtual<uint8_t>(mover, 0x0, uint32_t{0x8B99EE7C}, &path)) {
            if (walk) {
                Virtual<void>(mover, 0xCC, uint32_t{7}, uint32_t{0}, uint32_t{0xBF800000}, uint32_t{0},
                              static_cast<uint32_t>(index >= 0 ? index : 0), uint32_t{0}, uint32_t{0});
            } else {
                ok = Virtual<uint8_t>(path, 0x244, route, uint32_t{1}, uint32_t{0}) != 0;
                if (index >= 0)
                    Virtual<void>(mover, 0x4C, index);
                Virtual<void>(mover, 0xCC, uint32_t{0}, uint32_t{0}, uint32_t{0xBF800000}, uint32_t{0}, uint32_t{0},
                              uint32_t{0}, uint32_t{0});
            }
            if (ok) {
                reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySimDispatch_SetVehicle)(
                    self, Virtual<void*>(mover, 0x14));
                result = status;
                At<int32_t>(self, 0x15C) = kind;
                float position[3];
                Virtual<void>(At<void*>(self, field::kVehicle), 0x20, position);
                reinterpret_cast<void(__thiscall*)(void*, float, float)>(addr::SC4MySimPlace_SetPosition)(
                    self + field::kHere, position[0], position[2]);
                launched = true;
            }
        }
        if (!launched) {
            Virtual<void>(mover, 0xA0, self ? self + 8 : nullptr);
            Virtual<void>(mover, 0x34, uint32_t{0});
        }
        Release(path);
    }
    Release(mover);
    return result;
}

uint32_t __fastcall StartSegment(char* self, void*, void* vehicle, uint32_t walking)
{
    uint32_t local[10] = {};
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4RouteDeque_Init)(local, 0);
    uint32_t result = 0xFF;
    if (reinterpret_cast<uint8_t(__thiscall*)(void*, void*, uint32_t)>(addr::cSC4MySimDispatch_CopyRoute)(
            self, local, 0) &&
        static_cast<uint32_t>(reinterpret_cast<int32_t(__thiscall*)(void*)>(addr::SC4CellDeque_Size)(local)) > 1)
        result = reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*, uint32_t)>(addr::cSC4MySimDispatch_Launch)(
            self, local, vehicle, walking);
    reinterpret_cast<ThisFn>(addr::SC4RouteDeque_Destroy)(local);
    return result;
}

void KeepShown(char* self, bool shown)
{
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    ChangeFlags(self, shown ? flags | 0x100 : flags & 0xFFFFFEFF);
}

int32_t __fastcall ShowLocation(char* self, void*, uint32_t icon, char* text)
{
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x100);
    At<uint32_t>(self, 0x100) = icon;
    char* const from = text ? text + 4 : nullptr;
    if (from != self + 0x108) {
        uint8_t allocator;
        reinterpret_cast<void(__thiscall*)(void*, char*, char*, uint8_t*)>(addr::SC4String_AssignRange)(
            self + 0x108, At<char*>(from, 0), At<char*>(from, 4), &allocator);
    }
    const bool shown = (At<uint32_t>(self, field::kFlags) >> 8) & 1;
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_HideLocation)(self);
    KeepShown(self, shown);
    if (((At<uint32_t>(self, field::kFlags) >> 2) & 1) || At<int32_t>(self, field::kMarker) != -1)
        return At<int32_t>(self, field::kMarker);
    void* const overlays = Virtual<void*>(Global<void*>(addr::SC4_DispatchService), 0x58);
    void* const mover = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4MySimDispatch_Mover)(self);
    if (mover) {
        const int32_t id = reinterpret_cast<int32_t(__thiscall*)(void*, void*, uint32_t, uint32_t, char*)>(
            addr::SC4OverlayManager_Add)(overlays, mover, 3, icon, icon ? nullptr : text);
        At<int32_t>(self, field::kMarker) = id;
        if (id)
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4RefPtr_Assign)(self + field::kShown, mover);
        else
            At<int32_t>(self, field::kMarker) = -1;
        if (mover == At<void*>(self, field::kVehicle) && At<uint8_t>(self, 0x10)) {
            void* automaton = nullptr;
            void** const slot = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&automaton);
            reinterpret_cast<uint8_t(__thiscall*)(void*, void**)>(addr::cSC4MySimDispatch_VehicleAutomaton)(self,
                                                                                                             slot);
            Virtual<void>(Global<void*>(addr::SC4_Service43D44), 0x88, automaton);
            Release(automaton);
        }
    }
    return At<int32_t>(self, field::kMarker);
}

uint8_t __fastcall RefreshMarker(char* self, void*)
{
    if (At<int32_t>(self, 0x38) == At<int32_t>(self, field::kChanges) &&
        At<void*>(self, field::kShown) == reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4MySimDispatch_Mover)(self))
        return 1;
    const bool shown = (At<uint32_t>(self, field::kFlags) >> 8) & 1;
    if (At<void*>(self, field::kShown) || At<int32_t>(self, field::kMarker) != -1) {
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_HideLocation)(self);
        KeepShown(self, shown);
    }
    if (shown)
        reinterpret_cast<int32_t(__thiscall*)(void*, uint32_t, char*)>(addr::cSC4MySimDispatch_ShowLocation)(
            self, At<uint32_t>(self, 0x100), self + 0x104);
    KeepShown(self, shown);
    return 1;
}

bool InstallVehicle(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySimDispatch_MakeVehicle, reinterpret_cast<void*>(&MakeVehicle)},
        {addr::cSC4MySimDispatch_Launch, reinterpret_cast<void*>(&Launch)},
        {addr::cSC4MySimDispatch_StartSegment, reinterpret_cast<void*>(&StartSegment)},
        {addr::cSC4MySimDispatch_ShowLocation, reinterpret_cast<void*>(&ShowLocation)},
        {addr::cSC4MySimDispatch_RefreshMarker, reinterpret_cast<void*>(&RefreshMarker)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_vehicleRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_vehicle",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_Launch,
    "Our cSC4MySimDispatch vehicle making and launching and the location marker (docs/mysim-agents.md)",
    false,
    InstallVehicle,
});

// --- Legs -------------------------------------------------------------------------------

uint8_t __fastcall GoTo(char* self, void*, const Place* place)
{
    Place copy;
    CopyPlace(reinterpret_cast<char*>(&copy), place);
    void* const object = copy.object;
    if (object && static_cast<int32_t>(copy.extra[0]) < At<int32_t>(self, 0x20))
        copy.extra[0] = At<uint32_t>(self, 0x20);
    if (At<int32_t>(self, field::kState) != 2) {
        At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) - 1;
        reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_Clear)(self + field::kPlaces);
        auto* const places = reinterpret_cast<PlaceDeque*>(self + field::kPlaces);
        if (places->lastCur != places->lastEnd - 0x28) {
            if (places->lastCur)
                CopyPlace(places->lastCur, &copy);
            places->lastCur += 0x28;
        } else {
            reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4MySimPlaceDeque_PushBackSlow)(places,
                                                                                                          &copy);
        }
        Release(copy.object);
        return 1;
    }
    reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_Clear)(self + field::kPlaces);
    Assign(self, field::kTarget, reinterpret_cast<const char*>(&copy));
    void* automaton = nullptr;
    bool busy = false;
    if (void* const vehicle = At<void*>(self, field::kVehicle))
        busy = Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton) && Virtual<uint8_t>(automaton, 0x7C, uint32_t{1});
    uint8_t result = 1;
    if (busy) {
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFA7FF);
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFBFFF);
    } else {
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFA7FF);
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x4000);
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Step)(self);
        char* const route = self + field::kRoute;
        if (At<char*>(route, 0x10) == At<char*>(route, 0) || SegmentStart(self) == SegmentEnd(self)) {
            reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Finish)(self);
            result = 0;
        }
    }
    Release(automaton);
    Release(object);
    return result;
}

uint8_t __fastcall NextLeg(char* self, void*)
{
    if (!Virtual<uint8_t>(self, 0x18)) // IsArrived
        return 0;
    const int32_t state = At<int32_t>(self, field::kState);
    if (state == 2) {
        void* automaton = nullptr;
        if (void* const vehicle = At<void*>(self, field::kVehicle))
            Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton);
        if (!automaton)
            return 1;
        if (char* const part = Virtual<char*>(Global<void*>(addr::SC4_Service43D34), 0x1C, uint32_t{0x1D}))
            Virtual<void>(part - 8, 0x28);
        if (void* const traffic = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4)) {
            bool parked = false;
            if (Virtual<uint8_t>(traffic, 0x4C, uint32_t{0x4209})) {
                Virtual<void>(traffic, 0x64, automaton, uint32_t{0x4209});
                parked = Virtual<uint8_t>(traffic, 0x58, Virtual<void*>(automaton, 0x14), uint32_t{0}) != 0;
            }
            if (!parked)
                Virtual<void>(traffic, 0x60, automaton, uint32_t{1}, self + 8);
        } else {
            reinterpret_cast<void(__cdecl*)(void*, void*)>(addr::SC4_ParkAutomaton)(automaton, self + 8);
        }
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x10000);
        Release(automaton);
        return 1;
    }
    if (state != 3)
        return 0;
    using GoToFn = uint8_t(__thiscall*)(void*, const void*);
    if (Same(self, field::kHere, field::kHome)) {
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x20000);
        reinterpret_cast<GoToFn>(addr::cSC4MySimDispatch_GoTo)(self, self + field::kWork);
        return 1;
    }
    if (Same(self, field::kHere, field::kWork)) {
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x20000);
        reinterpret_cast<GoToFn>(addr::cSC4MySimDispatch_GoTo)(self, self + field::kHome);
        return 1;
    }
    return 0;
}

bool InstallLegs(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySimDispatch_GoTo, reinterpret_cast<void*>(&GoTo)},
        {addr::cSC4MySimDispatch_NextLeg, reinterpret_cast<void*>(&NextLeg)},
    };
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_legsRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_legs",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_GoTo,
    "Our cSC4MySimDispatch GoTo and next leg (docs/mysim-agents.md)",
    false,
    InstallLegs,
});

// --- Step -------------------------------------------------------------------------------

void RefreshMarkerNow(char* self)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_RefreshMarker)(self);
}

bool PlaceValid(char* self, std::size_t at)
{
    return IsValidPlace(self + at) != 0;
}

bool PlaceDiffers(const void* a, const void* b)
{
    return reinterpret_cast<int32_t(__thiscall*)(const void*, const void*)>(addr::SC4MySimPlace_Differ)(a, b) != 0;
}

void SetStateTo(char* self, int32_t state)
{
    const int32_t previous = At<int32_t>(self, field::kState);
    At<int32_t>(self, field::kPrevious) = previous;
    At<int32_t>(self, field::kState) = state;
    if (previous != state) {
        Post(self, kEventState, static_cast<uint32_t>(previous), static_cast<uint32_t>(state));
        ++At<int32_t>(self, field::kChanges);
    }
}

void* RouteView(char* self)
{
    return At<void*>(self, field::kRouteView);
}

struct Rect4 {
    int32_t v[4];
};

Rect4 PlaceRect(char* self, std::size_t at)
{
    Rect4 rect;
    reinterpret_cast<int32_t*(__thiscall*)(void*, int32_t*, const void*)>(addr::SC4MySimPlace_Rect)(self, rect.v,
                                                                                                     self + at);
    return rect;
}

bool FindConnection(char* self, std::size_t at)
{
    return reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t, void*)>(addr::cSC4MySimDispatch_FindConnection)(
               self, At<int32_t>(self, 0x58) + 1, self + at) != 0;
}

void PushCell(char* self, const uint8_t* cell)
{
    reinterpret_cast<void(__thiscall*)(void*, const uint8_t*)>(addr::SC4RouteDeque_PushBack)(self + field::kRoute,
                                                                                              cell);
}

// Stopped (state 3): wait out the time at the place, then the next place
// (queued, or home), travelling to it.
void StepStopped(char* self)
{
    reinterpret_cast<ThisFn>(addr::SC4MySimPlace_Reset)(self + field::kTarget);
    if (At<void*>(self, field::kHere))
        reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySimDispatch_SetVehicle)(self, nullptr);
    if (At<int32_t>(self, field::kNow) <= At<int32_t>(self, 0x80) && PlaceValid(self, field::kHere) &&
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4MySimDispatch_Mover)(self))
        return;
    if (!PlaceValid(self, field::kHere)) {
        bool done = false;
        if (At<uint8_t>(self, 0x79)) {
            if (PlaceValid(self, field::kHere) ||
                reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySimDispatch_PlanOutside)(
                    self, self + field::kHere)) {
                Assign(self, field::kWork, self + field::kHere);
                Post(self, 0xEBA06BF4, 0, 0);
                done = true;
            }
        }
        if (!done && PlaceValid(self, field::kHome)) {
            Assign(self, field::kHere, self + field::kHome);
            Post(self, 0xEBA06C12, 0, 0);
        }
    }
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    ChangeFlags(self, At<uint8_t>(self, 0x78) ? flags & 0xFFFFFFFB : flags | 4);
    auto* const places = reinterpret_cast<PlaceDeque*>(self + field::kPlaces);
    while (places->lastCur != places->firstCur && !IsValidPlace(places->firstCur))
        reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_PopFront)(places);
    Place next;
    Construct(&next, nullptr);
    if (places->lastCur != places->firstCur) {
        reinterpret_cast<void(__thiscall*)(Place*, const void*)>(addr::SC4MySimPlace_Assign)(&next, places->firstCur);
        reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_PopFront)(places);
    } else if (((At<uint32_t>(self, field::kFlags) >> 7) & 1) && PlaceDiffers(self + field::kHere, self + field::kHome) &&
               PlaceValid(self, field::kHome)) {
        reinterpret_cast<void(__thiscall*)(Place*, const void*)>(addr::SC4MySimPlace_Assign)(&next, self + field::kHome);
    }
    At<int32_t>(self, 0x58) = 0;
    if (next.work && !IsValidPlace(&next) &&
        reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySimDispatch_PlanOutside)(self, &next)) {
        Assign(self, field::kWork, reinterpret_cast<const char*>(&next));
        Post(self, 0xEBA06BF4, 0, 0);
    }
    if (IsValidPlace(&next)) {
        if (PlaceDiffers(self + field::kHere, &next)) {
            Assign(self, field::kTarget, reinterpret_cast<const char*>(&next));
            At<int32_t>(self, 0xD0) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0xCC);
            SetStateTo(self, 2);
            ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFA7FF);
        }
    } else if (!((At<uint32_t>(self, field::kFlags) >> 7) & 1) ||
               PlaceDiffers(self + field::kHere, self + field::kHome)) {
        SetStateTo(self, -1);
    }
    Release(next.object);
}

// The target rectangle Step gave the route view when it started a search.
// With flag 0x800 (only ever from a save) the game's Step reads it from
// stack slots this call did not write, so from an earlier call; the last
// values computed stand in for those.
Rect4 g_lastTarget;

// Travelling (state 2): the route searched for, then segment by segment
// with the vehicle (EndSegment when one ends).
void StepTravelling(char* self)
{
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    if ((flags >> 12) & 1) {
        if ((flags >> 16) & 1)
            return;
        if ((flags >> 6) & 1) {
            SetStateTo(self, -1);
            RefreshMarkerNow(self);
            return;
        }
        if ((flags >> 10) & 1) {
            reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_EndSegment)(self);
            RefreshMarkerNow(self);
            return;
        }
        if ((flags >> 9) & 1) {
            RefreshMarkerNow(self);
            return;
        }
        const uint32_t status = reinterpret_cast<uint32_t(__thiscall*)(void*, void*, uint32_t)>(
            addr::cSC4MySimDispatch_StartSegment)(self, nullptr, 0);
        if (status == 0xFF) {
            reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Finish)(self);
            RefreshMarkerNow(self);
            return;
        }
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x200);
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFFBFF);
        reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t)>(addr::cSC4MySimDispatch_SetStatus)(self, status);
        RefreshMarkerNow(self);
        return;
    }
    bool moving = false;
    if (!((flags >> 11) & 1)) {
        const bool replan = (flags >> 14) & 1;
        const uint32_t status = At<uint32_t>(self, field::kStatus);
        moving = replan && At<void*>(self, field::kVehicle) && status != 0xFF && status != 0xFE;
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_ClearRoute)(self);
        Virtual<void>(RouteView(self), 0x20, self + field::kRoute);
        Virtual<void>(RouteView(self), 0x14, uint32_t{1});
        Virtual<void>(RouteView(self), 0x14, uint32_t{0x10});
        Virtual<void>(RouteView(self), 0x48, uint32_t{1});
        bool last;
        if (replan && At<uint32_t>(self, field::kStatus) != 0xFF && At<uint32_t>(self, field::kStatus) != 0xFE) {
            const auto now = [self] { return At<uint32_t>(self, field::kStatus); };
            Virtual<void>(RouteView(self), 0x34, uint32_t{2}, uint32_t{now() == 1});
            Virtual<void>(RouteView(self), 0x34, uint32_t{1}, uint32_t{now() == 0});
            Virtual<void>(RouteView(self), 0x34, uint32_t{4}, uint32_t{now() == 2});
            Virtual<void>(RouteView(self), 0x34, uint32_t{8}, uint32_t{now() == 3});
            Virtual<void>(RouteView(self), 0x34, uint32_t{0x40}, uint32_t{now() == 6});
            Virtual<void>(RouteView(self), 0x34, uint32_t{0x80}, uint32_t{now() == 7});
            last = now() == 8;
        } else {
            const bool turning = (At<uint32_t>(self, field::kFlags) >> 17) & 1;
            Virtual<void>(RouteView(self), 0x34, uint32_t{2}, uint32_t{1});
            Virtual<void>(RouteView(self), 0x34, uint32_t{1}, uint32_t{!turning});
            const auto transit = [self, turning] { return !At<uint8_t>(self, 0x78) && !turning; };
            Virtual<void>(RouteView(self), 0x34, uint32_t{4}, uint32_t{transit()});
            Virtual<void>(RouteView(self), 0x34, uint32_t{8}, uint32_t{transit()});
            Virtual<void>(RouteView(self), 0x34, uint32_t{0x40}, uint32_t{transit()});
            Virtual<void>(RouteView(self), 0x34, uint32_t{0x80}, uint32_t{transit()});
            last = transit();
        }
        Virtual<void>(RouteView(self), 0x34, uint32_t{0x100}, uint32_t{last});
        Virtual<void>(RouteView(self), 0x24, uint32_t{3});
        if (At<uint8_t>(self, field::kTarget + 0x1D))
            Assign(self, field::kWork, self + field::kTarget);
        Virtual<void>(RouteView(self), 0x4C, PlaceRect(self, field::kHere));
        g_lastTarget = PlaceRect(self, field::kTarget);
        Virtual<void>(RouteView(self), 0x50, g_lastTarget);
    }
    const int32_t found = Virtual<int32_t>(RouteView(self), 0x5C);
    if (found == 2) {
        RefreshMarkerNow(self);
        return;
    }
    if (found != 1) {
        if (!At<uint8_t>(self, field::kTarget + 0x1C)) {
            if (FindConnection(self, field::kTarget)) {
                Virtual<void>(RouteView(self), 0x50, PlaceRect(self, field::kTarget));
                RefreshMarkerNow(self);
                return;
            }
        } else if (!At<uint8_t>(self, 0x78) && FindConnection(self, field::kHere)) {
            Virtual<void>(RouteView(self), 0x4C, PlaceRect(self, field::kHere));
            RefreshMarkerNow(self);
            return;
        }
        SetStateTo(self, -1);
        Post(self, 0xEBA06C12, 0, 0);
        RefreshMarkerNow(self);
        return;
    }
    const int32_t size = RouteSize(self);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFF7DF);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x1000);
    const bool walking = !moving || (At<uint8_t>(self, field::kFlags) & 1);
    reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(addr::cSC4MySimDispatch_SetFlags)(
        self, 0x400, walking ? 1 : 0);
    SegmentStart(self) = 0;
    SegmentEnd(self) = 0;
    const uint8_t* const lastCell =
        reinterpret_cast<uint8_t*(__thiscall*)(void*, int32_t)>(addr::SC4CellDeque_At)(self + field::kRoute, size - 1);
    const Rect4& target = g_lastTarget;
    if (target.v[2] == target.v[0] && target.v[3] == target.v[1]) {
        const uint8_t cell[3] = {static_cast<uint8_t>(target.v[0]), static_cast<uint8_t>(target.v[1]), lastCell[2]};
        PushCell(self, cell);
    }
    const uint8_t stop[3] = {lastCell[0], lastCell[1], 0xFE};
    PushCell(self, stop);
    reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_AdvanceRoute)(self);
    if (moving) {
        void* path = nullptr;
        void** const slot = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&path);
        Virtual<uint8_t>(At<void*>(self, field::kVehicle), 0x0, uint32_t{0x8B99EE7C}, slot);
        uint32_t local[10];
        uint8_t allocator;
        reinterpret_cast<void(__thiscall*)(void*, uint8_t*)>(addr::SC4RouteDeque_Construct)(local, &allocator);
        reinterpret_cast<uint8_t(__thiscall*)(void*, void*, uint32_t)>(addr::cSC4MySimDispatch_CopyRoute)(self, local,
                                                                                                            0);
        Virtual<uint8_t>(path, 0x244, local, uint32_t{1}, uint32_t{0});
        reinterpret_cast<ThisFn>(addr::SC4RouteDeque_Destroy)(local);
        Release(path);
        RefreshMarkerNow(self);
        return;
    }
    Post(self, 0x4BB71A7E, 0, 0);
    RefreshMarkerNow(self);
}

// No state (-1): back home if there is one, stopped there.
void StepNone(char* self)
{
    Place empty;
    Construct(&empty, nullptr);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFF01B2);
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    bool home = false;
    if ((flags >> 7) & 1) {
        if (!PlaceDiffers(self + field::kHere, self + field::kHome)) {
            reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t)>(addr::cSC4MySimDispatch_SetState)(self, 3);
        } else {
            ChangeFlags(self, flags & 0xFFFFFFCD);
            home = ((At<uint32_t>(self, field::kFlags) >> 7) & 1) &&
                   PlaceDiffers(self + field::kHere, self + field::kHome);
        }
    }
    if (!home) {
        auto* const places = reinterpret_cast<PlaceDeque*>(self + field::kPlaces);
        if (places->lastCur != places->firstCur)
            reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t)>(addr::cSC4MySimDispatch_SetState)(self, 3);
        RefreshMarkerNow(self);
        return;
    }
    reinterpret_cast<void(__thiscall*)(Place*, const void*)>(addr::SC4MySimPlace_Assign)(&empty, self + field::kHome);
    Assign(self, field::kHere, reinterpret_cast<const char*>(&empty));
    if (At<uint32_t>(self, field::kStatus) != 0xFE)
        ++At<int32_t>(self, field::kChanges);
    At<uint32_t>(self, field::kStatus) = 0xFE;
    ++At<int32_t>(self, field::kChanges);
    At<int32_t>(self, 0x80) = At<int32_t>(self, 0x7C) + At<int32_t>(self, field::kNow);
    Post(self, 0xEBA06C12, 0, 0);
    if (void* const vehicle = At<void*>(self, field::kVehicle)) {
        void* automaton = nullptr;
        Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton);
        if (automaton) {
            Virtual<void>(automaton, 0xA0, self + 8);
            Virtual<void>(automaton, 0x34, uint32_t{0});
            if (void* const old = At<void*>(self, field::kVehicle)) {
                At<void*>(self, field::kVehicle) = nullptr;
                Release(old);
            }
            Release(automaton);
        }
    }
    const int32_t previous = At<int32_t>(self, field::kState);
    At<int32_t>(self, field::kPrevious) = previous;
    At<int32_t>(self, field::kState) = 3;
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 2);
    PostStateChange(self);
    Release(empty.object);
    RefreshMarkerNow(self);
}

uint8_t __fastcall Step(char* self, void*)
{
    switch (At<int32_t>(self, field::kState)) {
    case -1:
        StepNone(self);
        break;
    case 2:
        StepTravelling(self);
        break;
    case 3:
        StepStopped(self);
        RefreshMarkerNow(self);
        break;
    default:
        RefreshMarkerNow(self);
        break;
    }
    return 1;
}

// --- EndSegment -------------------------------------------------------------------------

// A std::list the game's way (an empty sentinel from the shared node
// allocator).
void* NewList()
{
    void* const node = Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC});
    At<void*>(node, 0) = node;
    At<void*>(node, 4) = node;
    return node;
}

void ClearList(void** list)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4List_Clear2)(list);
}

void DestroyList(void** list)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4List_Destroy2)(list);
}

uint32_t StartSegmentWith(char* self, void* vehicle, uint32_t walking)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*, void*, uint32_t)>(addr::cSC4MySimDispatch_StartSegment)(
        self, vehicle, walking);
}

bool IsBuildingPlace(char* self, std::size_t at)
{
    return reinterpret_cast<uint8_t(__thiscall*)(void*, const void*)>(addr::SC4MySimPlace_IsBuilding)(self,
                                                                                                       self + at) != 0;
}

int32_t PlaceDistance(char* self, std::size_t a, std::size_t b)
{
    return reinterpret_cast<int32_t(__thiscall*)(void*, const void*, const void*)>(addr::SC4MySimPlace_Distance)(
        self, self + a, self + b);
}

void DropHereObject(char* self)
{
    if (void* const object = At<void*>(self, field::kHere)) {
        At<void*>(self, field::kHere) = nullptr;
        Release(object);
    }
}

void SetVehicleTo(char* self, void* vehicle)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4MySimDispatch_SetVehicle)(self, vehicle);
}

struct SegmentResult {
    uint32_t status;
    int32_t state;
    uint32_t set = 0;
    uint32_t clear = 0;
};

// Back on the route's previous cell when there is more route ahead.
bool ResumeRoute(char* self, SegmentResult& r, uint32_t clear)
{
    if (!reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4MySimDispatch_HasRouteAhead)(self))
        return false;
    const uint8_t* const cell = reinterpret_cast<uint8_t*(__thiscall*)(void*, int32_t)>(addr::SC4CellDeque_At)(
        self + field::kRoute, SegmentStart(self) - 1);
    const uint32_t x = cell[0];
    const uint32_t z = cell[1];
    r.status = cell[2];
    r.set = 0x220;
    r.clear = clear;
    reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t)>(addr::SC4MySimPlace_Clear)(
        self + field::kHere, static_cast<int32_t>(x), static_cast<int32_t>(z));
    return true;
}

void StopHere(char* self, SegmentResult& r)
{
    At<int32_t>(self, 0x80) = At<int32_t>(self, 0x7C) + At<int32_t>(self, field::kNow);
    r.state = 3;
    r.clear = 0xA403;
}

// In or by the car (flag 1): walking to it (0x2000), the car's model, or
// getting out near the target.
void SegmentInCar(char* self, SegmentResult& r)
{
    void* automaton = nullptr;
    if (void* const vehicle = At<void*>(self, field::kVehicle))
        Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton);
    const int32_t* const info = automaton ? Virtual<const int32_t*>(automaton, 0xE0) : nullptr;
    bool walkingToCar = (At<uint32_t>(self, field::kFlags) >> 13) & 1;
    r.clear = 0x2002;
    int32_t index = -1;
    if (walkingToCar) {
        Post(self, 0xEBA06BD0, 0, 0);
        index = reinterpret_cast<int32_t(__thiscall*)(void*, int32_t)>(addr::cSC4MySimDispatch_ModelIndex)(
            self, At<int32_t>(self, 0x15C));
    }
    const uint32_t status = At<uint32_t>(self, field::kStatus);
    if (!(automaton && At<int32_t>(self, 0x15C) == 2 && info && info[1] == 7 && (status == 0 || status == 0xFE))) {
        Place copy;
        CopyPlace(reinterpret_cast<char*>(&copy), self + field::kHere);
        using AtFn = uint8_t*(__thiscall*)(void*, int32_t);
        const auto cellAt = [self](int32_t index) {
            return reinterpret_cast<AtFn>(addr::SC4CellDeque_At)(self + field::kRoute, index);
        };
        const int32_t start = SegmentStart(self);
        if (cellAt(start)[2] != 0xFE) {
            const int32_t x = cellAt(start)[0];
            copy.cellX = x;
            const int32_t z = cellAt(start)[1];
            copy.cellZ = z;
            copy.x = static_cast<float>(x << 4);
            copy.z = static_cast<float>(z << 4);
        }
        using SnapFn = uint8_t(__thiscall*)(void*, void*);
        if (reinterpret_cast<SnapFn>(addr::cSC4MySimDispatch_SnapToRoad)(self, self + field::kHere) ||
            reinterpret_cast<SnapFn>(addr::cSC4MySimDispatch_SnapToRoad)(self, &copy)) {
            Assign(self, field::kHere, reinterpret_cast<const char*>(&copy));
            uint32_t local[10] = {};
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4RouteDeque_Init)(local, 0);
            const uint8_t cell[3] = {cellAt(SegmentStart(self))[0], cellAt(SegmentStart(self))[1], 0};
            auto& end = *reinterpret_cast<CellIterator*>(reinterpret_cast<char*>(local) + 0x10);
            if (end.cur != end.last - 3) {
                if (end.cur) {
                    end.cur[0] = cell[0];
                    end.cur[1] = cell[1];
                    end.cur[2] = cell[2];
                }
                end.cur += 3;
            } else {
                reinterpret_cast<void(__thiscall*)(void*, const uint8_t*)>(addr::SC4RouteDeque_PushBackSlow)(local,
                                                                                                          cell);
            }
            Post(self, 0xEBA06BD0, 0, 0);
            const uint32_t launched = reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*, uint32_t)>(
                addr::cSC4MySimDispatch_Launch)(self, local, nullptr, 1);
            if (launched == 0xFF || At<uint32_t>(self, field::kStatus) != 0xFE)
                r.status = launched;
            if (launched == 0xFF) {
                Post(self, 0xEBA06C12, 0, 0);
                r.state = -1;
            }
            reinterpret_cast<ThisFn>(addr::SC4RouteDeque_Destroy)(local);
        } else {
            Post(self, 0xEBA06C12, 0, 0);
            r.state = -1;
            walkingToCar = false;
        }
        Release(copy.object);
    }
    if (walkingToCar) {
        if (automaton) {
            void* const old = automaton;
            automaton = nullptr;
            Release(old);
        }
        if (void* const vehicle = At<void*>(self, field::kVehicle))
            Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton);
        if (At<int32_t>(self, 0x80) >= At<int32_t>(self, field::kNow) && automaton) {
            if (index > 0) {
                Virtual<void>(automaton, 0x4C, index);
                r.clear = 0x2002;
            }
        } else {
            ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFFFFE);
            const int32_t reach = At<int32_t>(self, 0x18);
            const bool near = PlaceDistance(self, field::kTarget, field::kHere) < reach * reach;
            if (At<uint32_t>(self, field::kStatus) == 0xFE && ((At<uint32_t>(self, field::kFlags) >> 5) & 1)) {
                if (IsBuildingPlace(self, field::kTarget) && near)
                    Assign(self, field::kHere, self + field::kTarget);
                StopHere(self, r);
            } else {
                const uint32_t status = StartSegmentWith(self, nullptr, 0);
                r.status = status;
                if (status != 0xFF) {
                    r.set = 0x200;
                    r.clear = 0x2437;
                } else if (!ResumeRoute(self, r, 0x2002)) {
                    if (near) {
                        if (IsBuildingPlace(self, field::kTarget))
                            Assign(self, field::kHere, self + field::kTarget);
                        StopHere(self, r);
                    } else {
                        Post(self, 0xEBA06C12, 0, 0);
                        r.state = -1;
                    }
                }
            }
        }
    }
    Release(automaton);
}

struct TransitKind {
    uint32_t model;
    uint32_t stop;
};

// On a transit segment (flag 0x10): to the stop for the kind, then onto a
// vehicle of the line there.
void SegmentTransit(char* self, SegmentResult& r)
{
    const uint32_t kind = reinterpret_cast<uint8_t*(__thiscall*)(void*, int32_t)>(addr::SC4CellDeque_At)(
        self + field::kRoute, SegmentStart(self))[2];
    void* const network = Global<void*>(addr::SC4_NetworkService);
    void* found = NewList();
    void* holder = nullptr;
    if (!At<void*>(self, field::kHere)) {
        uint32_t stop = 0;
        switch (kind) {
        case 2:
            At<uint32_t>(self, 0x28) = 0x4200;
            stop = 0x1301;
            break;
        case 3:
            At<uint32_t>(self, 0x28) = 0x4215;
            stop = 0x1300;
            break;
        case 6:
            At<uint32_t>(self, 0x28) = 0x4217;
            stop = 0x1302;
            break;
        case 7:
            At<uint32_t>(self, 0x28) = 0x422C;
            stop = 0x1303;
            break;
        case 8:
            At<uint32_t>(self, 0x28) = 0x422E;
            stop = 0x1307;
            break;
        default:
            break;
        }
        using AtFn = uint8_t*(__thiscall*)(void*, int32_t);
        int32_t x0 = At<int32_t>(self, 0x68);
        int32_t z0 = At<int32_t>(self, 0x6C);
        int32_t x1 = reinterpret_cast<AtFn>(addr::SC4CellDeque_At)(self + field::kRoute, SegmentStart(self))[0];
        int32_t z1 = reinterpret_cast<AtFn>(addr::SC4CellDeque_At)(self + field::kRoute, SegmentStart(self))[1];
        if (x0 > x1) {
            const int32_t t = x0;
            x0 = x1;
            x1 = t;
        }
        if (z0 > z1) {
            const int32_t t = z0;
            z0 = z1;
            z1 = t;
        }
        const int32_t low[2] = {x0 - 1, z0 - 1};
        const int32_t high[2] = {x1 + 1, z1 + 1};
        void* stops = NewList();
        Virtual<void>(network, 0xA0, &stops, low, high, uint32_t{0}, uint32_t{0xFFFFFFFF});
        for (void* node = At<void*>(stops, 0); node != stops;) {
            void* const occupant = At<void*>(node, 8);
            node = At<void*>(node, 0);
            if (Virtual<uint8_t>(occupant, 0x58, stop)) {
                reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4RefPtr_Assign)(self + field::kHere, occupant);
                float position[3];
                Virtual<void>(At<void*>(self, field::kHere), 0x20, position);
                reinterpret_cast<void(__thiscall*)(void*, float, float)>(addr::SC4MySimPlace_SetPosition)(
                    self + field::kHere, position[0], position[2]);
                At<int32_t>(self, 0x80) = At<int32_t>(self, 0x24) + At<int32_t>(self, field::kNow);
                At<int32_t>(self, 0x54) = At<int32_t>(self, field::kNow);
                Post(self, 0xEBA0732E, stop, 0);
                SetVehicleTo(self, nullptr);
                break;
            }
        }
        Virtual<void>(network, 0xC0, &stops);
        ClearList(&stops);
        DestroyList(&stops);
        if (!At<void*>(self, field::kHere)) {
            r.set = 0x40;
            DestroyList(&found);
            return;
        }
    }
    if (At<int32_t>(self, 0x80) < At<int32_t>(self, field::kNow)) {
        const uint32_t status = StartSegmentWith(self, nullptr, 0);
        r.status = status;
        if (status == 0xFF) {
            r.set = 0x40;
        } else {
            r.set = 0x200;
            r.clear = 0x436;
        }
    } else {
        int32_t cell[4] = {At<int32_t>(self, 0x68), At<int32_t>(self, 0x6C), At<int32_t>(self, 0x68),
                           At<int32_t>(self, 0x6C)};
        reinterpret_cast<void(__thiscall*)(int32_t*)>(addr::SC4Rect_Normalize)(cell);
        const int32_t low[2] = {cell[0], cell[1]};
        const int32_t high[2] = {cell[2], cell[3]};
        Virtual<void>(network, 0xA0, &found, low, high, uint32_t{0}, uint32_t{0xFFFFFFFF});
        for (void* node = At<void*>(found, 0); node != found;) {
            void* const occupant = At<void*>(node, 8);
            node = At<void*>(node, 0);
            if (holder) {
                void* const old = holder;
                holder = nullptr;
                Release(old);
            }
            if (occupant && Virtual<uint8_t>(occupant, 0x58, At<uint32_t>(self, 0x28)) &&
                Virtual<uint8_t>(occupant, 0x0, kIidAutomaton, &holder)) {
                Virtual<void>(holder, 0x9C, self + 8);
                Virtual<void>(holder, 0x78, uint32_t{0x1000000}, uint32_t{1});
                Virtual<void>(holder, 0x78, uint32_t{0x10000}, uint32_t{1});
                Virtual<void>(holder, 0x100, uint32_t{0xFF});
                const uint32_t status = StartSegmentWith(self, holder, 0);
                r.status = status;
                if (status == 0xFF) {
                    r.set = 0x40;
                } else {
                    r.set = 0x200;
                    r.clear = 0x436;
                }
                break;
            }
        }
        Virtual<void>(network, 0xC0, &found);
        ClearList(&found);
    }
    Release(holder);
    DestroyList(&found);
}

// At the end of a segment on foot (flag 0x20): arrived at a stop or the
// target, or onto the next kind of segment.
void SegmentWalking(char* self, SegmentResult& r, uint32_t status0)
{
    const uint32_t kind = reinterpret_cast<uint8_t*(__thiscall*)(void*, int32_t)>(addr::SC4CellDeque_At)(
        self + field::kRoute, SegmentStart(self))[2];
    r.clear = 0x20;
    if (kind == 0xFE) {
        r.clear = 0x460;
        r.set = 2;
        if (!At<uint8_t>(self, 0x78) || !At<uint8_t>(self, field::kTarget + 0x1C)) {
            r.set = 6;
            DropHereObject(self);
            SetVehicleTo(self, nullptr);
        }
        const int32_t reach = At<int32_t>(self, 0x18);
        if (PlaceDistance(self, field::kTarget, field::kHere) > reach * reach) {
            r.set |= 0x421;
            At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0x1C);
            Post(self, 0x2BAC398F, 0, 0);
            r.status = 0xFE;
            return;
        }
        if (!((At<uint32_t>(self, field::kFlags) >> 2) & 1) && !(r.set & 4) && !(At<uint8_t>(self, field::kFlags) & 1) &&
            PlaceDiffers(self + field::kTarget, self + field::kHome) &&
            PlaceDiffers(self + field::kTarget, self + field::kWork)) {
            r.set |= 0x421;
            At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0x1C);
        } else {
            At<int32_t>(self, 0x7C) = At<int32_t>(self, 0x7C) + At<int32_t>(self, field::kNow);
            r.clear = 0x8460;
            r.state = 3;
            if (IsBuildingPlace(self, field::kTarget))
                Assign(self, field::kHere, self + field::kTarget);
        }
        Post(self, 0xEBA06BD4, 0, 0);
        r.status = 0xFE;
        return;
    }
    if (kind == status0) {
        void* automaton = nullptr;
        void** const slot = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&automaton);
        reinterpret_cast<uint8_t(__thiscall*)(void*, void**)>(addr::cSC4MySimDispatch_VehicleAutomaton)(self, slot);
        const uint32_t status = StartSegmentWith(self, automaton, 0);
        if (status == 0xFF) {
            reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Finish)(self);
        } else {
            ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x200);
            ChangeFlags(self, At<uint32_t>(self, field::kFlags) & 0xFFFFFBFF);
            reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t)>(addr::cSC4MySimDispatch_SetStatus)(self, status);
        }
        Release(automaton);
        return;
    }
    const int32_t from = static_cast<int32_t>(status0);
    if (from > 8) {
        if (status0 >= 0xFE && status0 <= 0xFF)
            r.state = -1;
        return;
    }
    if (from < 6 && (from < 0 || from > 3))
        return;
    switch (kind) {
    case 0:
    case 1:
        r.set = 1;
        DropHereObject(self);
        At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0x1C);
        break;
    case 2:
    case 3:
    case 6:
    case 7:
    case 8:
        r.set = 0x10;
        DropHereObject(self);
        At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0x24);
        break;
    default:
        break;
    }
    SetVehicleTo(self, nullptr);
}

uint8_t __fastcall EndSegment(char* self, void*)
{
    SegmentResult r;
    r.state = At<int32_t>(self, field::kState);
    const uint32_t status0 = At<uint32_t>(self, field::kStatus);
    r.status = status0;
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    const bool inCar = At<uint8_t>(self, field::kFlags) & 1;
    if ((flags >> 6) & 1 && !inCar) {
        r.state = -1;
    } else if (inCar) {
        SegmentInCar(self, r);
    } else {
        bool done = false;
        if (status0 == 0xFE && !((flags >> 2) & 1) && PlaceDiffers(self + field::kHere, self + field::kHome) &&
            PlaceDiffers(self + field::kHere, self + field::kWork)) {
            At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0x1C);
            r.set = 1;
            done = true;
        }
        if (!done) {
            if ((flags >> 4) & 1) {
                SegmentTransit(self, r);
            } else if ((flags >> 2) & 1) {
                if (At<int32_t>(self, 0x80) < At<int32_t>(self, field::kNow)) {
                    const uint32_t status = StartSegmentWith(self, nullptr, 0);
                    r.status = status;
                    if (status == 0xFF) {
                        r.set = 0x40;
                    } else {
                        r.set = 0x200;
                        r.clear = 0x436;
                    }
                }
            } else if ((flags >> 5) & 1) {
                SegmentWalking(self, r, status0);
                if (!(r.set & 0x53)) {
                    r.set |= 3;
                    r.status = 0xFE;
                    reinterpret_cast<ThisFn>(addr::SC4MySimPlaceDeque_Clear)(self + field::kPlaces);
                    At<int32_t>(self, 0x80) = At<int32_t>(self, field::kNow) + At<int32_t>(self, 0x1C);
                }
            } else {
                const uint32_t status = StartSegmentWith(self, nullptr, 0);
                r.status = status;
                if (status != 0xFF) {
                    r.set = 0x200;
                    r.clear = 0x436;
                } else if (!ResumeRoute(self, r, 2)) {
                    Post(self, 0xEBA06C12, 0, 0);
                    r.state = -1;
                }
            }
        }
    }
    if (r.status != At<uint32_t>(self, field::kStatus))
        ++At<int32_t>(self, field::kChanges);
    At<uint32_t>(self, field::kStatus) = r.status;
    reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t)>(addr::cSC4MySimDispatch_SetState)(self, r.state);
    if (r.set & 0x40)
        reinterpret_cast<ThisFn>(addr::cSC4MySimDispatch_Finish)(self);
    if (r.set & 1)
        ChangeFlags(self, At<uint32_t>(self, field::kFlags) | 0x2401);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) & ~r.clear);
    ChangeFlags(self, At<uint32_t>(self, field::kFlags) | r.set);
    return 1;
}

bool InstallStep(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* original = nullptr;
    const addr::Entry* found = addr_check::Find(addr::cSC4MySimDispatch_Step);
    return found && hook::Install(*found, reinterpret_cast<void*>(&Step), &original);
}

bool InstallSegment(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* original = nullptr;
    const addr::Entry* found = addr_check::Find(addr::cSC4MySimDispatch_EndSegment);
    return found && hook::Install(*found, reinterpret_cast<void*>(&EndSegment), &original);
}

const bool g_stepRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_step",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_Step,
    "Our cSC4MySimDispatch Step, the trip state machine (docs/mysim-agents.md)",
    false,
    InstallStep,
});

const bool g_segmentRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_segment",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_EndSegment,
    "Our cSC4MySimDispatch EndSegment: car, transit and arrival at a segment's end (docs/mysim-agents.md)",
    false,
    InstallSegment,
});

// --- Save -------------------------------------------------------------------------------
//
// From the serializable part at +0xC (`ser`): the object's +X is ser+X-0xC.

uint32_t PlaceCount(const PlaceDeque* places)
{
    return static_cast<uint32_t>((places->lastNode - places->firstNode) * 3 +
                                 (places->lastCur - places->lastBegin) / 0x28 +
                                 (places->firstEnd - places->firstCur) / 0x28 - 3);
}

int32_t __fastcall WriteDispatch(char* ser, void*, void* stream)
{
    char* const self = ser - 0xC;
    Virtual<void>(stream, 0x20, uint32_t{1});
    reinterpret_cast<void(__cdecl*)(void*, uint32_t)>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(ser, 0x4));
    for (std::size_t at = 0x8; at <= 0x4C; at += 4)
        Virtual<void>(stream, 0x20, At<uint32_t>(ser, at));
    using PlaceWriteFn = int32_t(__thiscall*)(void*, void*);
    for (const std::size_t at : {field::kHere, field::kHome, field::kTarget, field::kWork})
        reinterpret_cast<PlaceWriteFn>(addr::SC4MySimPlace_Write)(self + at, stream);
    using WriteReferenceFn = void(__cdecl*)(void*, void*);
    const auto writeReference = reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference);
    writeReference(stream, At<void*>(self, field::kShown));
    Virtual<void>(stream, 0x20, At<uint32_t>(self, 0x100));
    Virtual<void>(stream, 0x3C, self + 0x104);
    writeReference(stream, At<void*>(self, field::kVehicle));
    char* const owner = At<char*>(self, field::kOwner);
    writeReference(stream, owner ? owner + 0xB8 : nullptr);
    writeReference(stream, At<void*>(self, 0x120));
    Virtual<void>(stream, 0x20, static_cast<uint32_t>(RouteSize(self)));
    CellIterator cell = *reinterpret_cast<CellIterator*>(self + field::kRoute);
    uint8_t* const routeEnd = At<uint8_t*>(self, field::kRoute + 0x10);
    while (cell.cur != routeEnd) {
        const uint8_t* const at = cell.cur;
        cell.cur += 3;
        if (cell.cur == cell.last) {
            ++cell.node;
            cell.cur = *cell.node;
            cell.first = cell.cur;
            cell.last = cell.cur + 0x7E;
        }
        Virtual<void>(stream, 0x14, uint32_t{at[0]});
        Virtual<void>(stream, 0x14, uint32_t{at[1]});
        Virtual<void>(stream, 0x14, uint32_t{at[2]});
    }
    auto* const header = At<MapNode*>(self, field::kModels);
    Virtual<void>(stream, 0x20, At<uint32_t>(self, field::kModels + 4));
    for (MapNode* node = header->left; node != header;) {
        MapNode* next;
        if (node->right) {
            next = node->right;
            while (next->left)
                next = next->left;
        } else {
            MapNode* child = node;
            next = node->parent;
            while (child == next->right) {
                child = next;
                next = next->parent;
            }
            if (child->right == next)
                next = child;
        }
        Virtual<void>(stream, 0x20, static_cast<uint32_t>(node->kind));
        reinterpret_cast<void(__cdecl*)(void*, const uint32_t*)>(addr::SC4Stream_WriteResourceKey)(stream, node->key);
        Virtual<void>(stream, 0x20, static_cast<uint32_t>(node->index));
        node = next;
    }
    Virtual<void>(stream, 0x20, At<uint32_t>(self, 0x15C));
    auto* const places = reinterpret_cast<PlaceDeque*>(self + field::kPlaces);
    Virtual<void>(stream, 0x20, PlaceCount(places));
    char* place = places->firstCur;
    char* last = places->firstEnd;
    char** node = places->firstNode;
    while (place != places->lastCur) {
        char* const at = place;
        place += 0x28;
        if (place == last) {
            ++node;
            place = *node;
            last = place + 0x78;
        }
        reinterpret_cast<PlaceWriteFn>(addr::SC4MySimPlace_Write)(at, stream);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

uint8_t __fastcall ReadDispatch(char* ser, void*, void* stream)
{
    char* const self = ser - 0xC;
    uint32_t version;
    Virtual<void>(stream, 0x20, &version);
    reinterpret_cast<void(__cdecl*)(void*, void*)>(addr::SC4Stream_ReadBool)(stream, ser + 0x4);
    for (std::size_t at = 0x8; at <= 0x30; at += 4)
        Virtual<void>(stream, 0x20, ser + at);
    for (const std::size_t at : {std::size_t{0x34}, std::size_t{0x38}}) {
        uint32_t value;
        Virtual<void>(stream, 0x20, &value);
        At<uint32_t>(ser, at) = value;
    }
    for (std::size_t at = 0x3C; at <= 0x4C; at += 4)
        Virtual<void>(stream, 0x20, ser + at);
    using PlaceReadFn = int32_t(__thiscall*)(void*, void*);
    for (const std::size_t at : {field::kHere, field::kHome, field::kTarget, field::kWork})
        reinterpret_cast<PlaceReadFn>(addr::SC4MySimPlace_Read)(self + at, stream);
    using ReadReferenceFn = void(__cdecl*)(void*, uint32_t, void*, uint32_t);
    const auto readReference = [stream](char* slot, uint32_t iid) {
        if (void* const old = At<void*>(slot, 0)) {
            At<void*>(slot, 0) = nullptr;
            Release(old);
        }
        reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, iid, slot, 0);
    };
    readReference(self + field::kShown, 0x26BB767E);
    Virtual<void>(stream, 0x20, self + 0x100);
    Virtual<void>(stream, 0x3C, self + 0x104);
    readReference(self + field::kVehicle, 0x26BB767E);
    readReference(self + field::kOwner, 0x8BC01FFE);
    readReference(self + 0x120, 0x681BD53A);
    int32_t count;
    Virtual<void>(stream, 0x20, &count);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t cell[3];
        Virtual<void>(stream, 0x14, &cell[0]);
        Virtual<void>(stream, 0x14, &cell[1]);
        Virtual<void>(stream, 0x14, &cell[2]);
        auto& end = *reinterpret_cast<CellIterator*>(self + field::kRoute + 0x10);
        if (end.cur != end.last - 3) {
            if (end.cur) {
                end.cur[0] = cell[0];
                end.cur[1] = cell[1];
                end.cur[2] = cell[2];
            }
            end.cur += 3;
        } else {
            reinterpret_cast<void(__thiscall*)(void*, const uint8_t*)>(addr::SC4RouteDeque_PushBackSlow)(
                self + field::kRoute, cell);
        }
    }
    struct {
        int32_t kind;
        uint32_t key[3];
        int32_t index;
    } model{};
    Virtual<void>(stream, 0x20, &count);
    for (int32_t i = 0; i < count; ++i) {
        Virtual<void>(stream, 0x20, &model.kind);
        reinterpret_cast<void(__cdecl*)(void*, uint32_t*)>(addr::SC4Stream_ReadResourceKey)(stream, model.key);
        Virtual<void>(stream, 0x20, &model.index);
        uint32_t result[2];
        reinterpret_cast<void(__thiscall*)(void*, uint32_t*, const void*)>(addr::SC4MySimModelMap_Insert)(
            self + field::kModels, result, &model);
    }
    uint32_t kind;
    Virtual<void>(stream, 0x20, &kind);
    At<uint32_t>(self, 0x15C) = kind;
    Place place;
    Construct(&place, nullptr);
    Virtual<void>(stream, 0x20, &count);
    auto* const places = reinterpret_cast<PlaceDeque*>(self + field::kPlaces);
    for (int32_t i = 0; i < count; ++i) {
        reinterpret_cast<PlaceReadFn>(addr::SC4MySimPlace_Read)(&place, stream);
        if (places->lastCur != places->lastEnd - 0x28) {
            if (places->lastCur)
                CopyPlace(places->lastCur, &place);
            places->lastCur += 0x28;
        } else {
            reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4MySimPlaceDeque_PushBackSlow)(places,
                                                                                                          &place);
        }
    }
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, self ? self + 8 : nullptr, uint32_t{0x26D31EC1});
    const uint8_t ok = Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
    At<uint8_t>(self, 0x188) = ok;
    Release(place.object);
    return ok;
}

bool InstallSave(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySimDispatch_Write, reinterpret_cast<void*>(&WriteDispatch)},
        {addr::cSC4MySimDispatch_Read, reinterpret_cast<void*>(&ReadDispatch)},
    };
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_save",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_Write,
    "Our cSC4MySimDispatch Write and Read (docs/mysim-agents.md)",
    false,
    InstallSave,
});

bool InstallState(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4MySimDispatch_PostEvent, reinterpret_cast<void*>(&PostEvent)},
        {addr::cSC4MySimDispatch_SetStatus, reinterpret_cast<void*>(&SetStatusThunk)},
        {addr::cSC4MySimDispatch_WaitLength, reinterpret_cast<void*>(&WaitLength)},
        {addr::cSC4MySimDispatch_SetFlags, reinterpret_cast<void*>(&SetFlags)},
        {addr::cSC4MySimDispatch_SetState, reinterpret_cast<void*>(&SetState)},
        {addr::cSC4MySimDispatch_Stop, reinterpret_cast<void*>(&Stop)},
        {addr::cSC4MySimDispatch_HideLocation, reinterpret_cast<void*>(&HideLocation)},
        {addr::cSC4MySimDispatch_Reshow, reinterpret_cast<void*>(&Reshow)},
        {addr::cSC4MySimDispatch_SetVehicle, reinterpret_cast<void*>(&SetVehicle)},
        {addr::cSC4MySimDispatch_Finish, reinterpret_cast<void*>(&Finish)},
        {addr::cSC4MySimDispatch_Mover, reinterpret_cast<void*>(&Mover)},
        {addr::cSC4MySimDispatch_VehicleAutomaton, reinterpret_cast<void*>(&VehicleAutomaton)},
        {addr::cSC4MySimDispatch_ModelService, reinterpret_cast<void*>(&ModelServiceThunk)},
        {addr::cSC4MySimDispatch_ModelKey, reinterpret_cast<void*>(&ModelKey)},
        {addr::cSC4MySimDispatch_ModelIndex, reinterpret_cast<void*>(&ModelIndex)},
        {addr::cSC4MySimDispatch_ModelFor, reinterpret_cast<void*>(&ModelFor)},
        {addr::cSC4MySimDispatch_ClearRoute, reinterpret_cast<void*>(&ClearRoute)},
        {addr::cSC4MySimDispatch_IsArrived, reinterpret_cast<void*>(&IsArrived)},
        {addr::cSC4MySimDispatch_Abort, reinterpret_cast<void*>(&Abort)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_stateRegistered = registry::Register({
    "simulators.mysim_agents.dispatch_state",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimDispatch_SetState,
    "Our cSC4MySimDispatch state, flags, events, vehicle and model lookups (docs/mysim-agents.md)",
    false,
    InstallState,
});

} // namespace
} // namespace opensc4::mysim_dispatch
