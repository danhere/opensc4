// cSC4MySimDispatch (docs/mysim-agents.md): the object that takes a
// MySim from place to place (cSC4MySim+0x20C). Replacements, each off by
// default:
//
//   simulators.mysim_agents.dispatch_state: the state, flags and status
//     setters, the events they post to the MySim, the vehicle, the model
//     lookups, the route reset and the vtable's arrived and abort slots;
//   simulators.mysim_agents.place: SC4MySimPlace, a place a MySim is at or
//     goes to (0x28 bytes: occupant +0, position +4/+8, cell +0xC/+0x10,
//     neighbour connection +0x14/+0x18, in the city +0x1C, work +0x1D).
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
        {addr::cSC4MySimDispatch_SetStatus, reinterpret_cast<void*>(&SetStatus)},
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
        {addr::cSC4MySimDispatch_ModelService, reinterpret_cast<void*>(&ModelService)},
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
