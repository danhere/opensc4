// The fire (docs/disasters.md): cSC4DisasterManagerFire (disaster
// 0x621CDA33) and cSC4DisasterInstanceFire: one instance holds every fire
// in the city, a fire being a fire occupant (cSC4FireOccupant, the game's)
// on a burning building, lot or flora. Replacement, off by default:
//
//   simulators.disasters.fire: the manager (QueryInterface, the start, the
//     fire part's calls, making the instance) and the instance (Init,
//     Shutdown, the tuning, starting, stepping, spreading, dousing and
//     removing fires, what a fire leaves, the messages).
//
// The objects' constructors and destructors and the fire occupant stay the
// game's. The instance: +8 the fire interface (0x49B43F58), +0xC the
// instance interface, +0x10 the message target.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::disasters {
namespace {

constexpr uint32_t kFire = 0x621CDA33;
constexpr uint32_t kBuilding = 0x278128A0;
constexpr uint32_t kLotType = 0xA823821E;
constexpr uint32_t kFlora = 0x74758926;
constexpr uint32_t kFlammability = 0x29244DB5;
constexpr uint32_t kBurning = 0x69DBEBE1;

namespace field { // the instance
constexpr std::size_t kReferences = 0x4;
constexpr std::size_t kLots = 0x14;         // 0x00B43D00
constexpr std::size_t kRandom = 0x18;
constexpr std::size_t kStopwatch = 0x30;
constexpr std::size_t kFilter = 0x48;       // the three burnable types
constexpr std::size_t kCellsX = 0x4C;
constexpr std::size_t kCellsZ = 0x50;
constexpr std::size_t kHold = 0x54;         // no burning down while set
constexpr std::size_t kStageLimits = 0x58;  // vector of 5, cumulative
constexpr std::size_t kSpreadOdds = 0x64;   // vector, by stage
constexpr std::size_t kRadii = 0x70;        // vector, by stage
constexpr std::size_t kWind = 0x7C;         // float
constexpr std::size_t kPeriod = 0x80;       // ms between steps
constexpr std::size_t kCurves = 0x84;       // four, 0x10 bytes each
constexpr std::size_t kZoom = 0xC4;         // uint8
constexpr std::size_t kInited = 0xC5;
constexpr std::size_t kNextStep = 0xC8;
constexpr std::size_t kState = 0xCC;
constexpr std::size_t kNewFires = 0xD0;
constexpr std::size_t kNewFloraFires = 0xD4;
constexpr std::size_t kAlert = 0xD8;
constexpr std::size_t kOwner = 0xDC;
constexpr std::size_t kFires = 0xE0;        // list of fire occupants (references)
constexpr std::size_t kDestroyed = 0xE4;
constexpr std::size_t kBurned = 0xEC;       // list of occupants to destroy
constexpr std::size_t kGroups = 0xF0;       // set of group IDs, count at +4
} // namespace field

using ThisFn = void(__thiscall*)(void* self);
using IntersectFn = uint8_t(__thiscall*)(int32_t* out, const int32_t* a, const int32_t* b);
using PushFn = void(__thiscall*)(void* vector, uint32_t* where, uint32_t* value, void* allocator, uint32_t count,
                                 uint32_t atEnd);

void* City()
{
    return Global<void*>(addr::SC4_City);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void* OperatorNew(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(size);
}

uint32_t Elapsed(char* self)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::GZStopwatch_ElapsedMilliseconds)(self + field::kStopwatch);
}

uint32_t Below(char* self, uint32_t count)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(addr::cRZRandom_Below)(self + field::kRandom, count);
}

uint32_t Advance(char* self)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cRZRandom_Advance)(self + field::kRandom);
}

double Evaluate(char* self, int which, float at)
{
    return reinterpret_cast<double(__thiscall*)(void*, float)>(addr::SC4Curve_Evaluate)(self + field::kCurves + 0x10 * which,
                                                                                       at);
}

int32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    return static_cast<int32_t>(bits + 0xB4C00000);
}

bool Empty(char* self, std::size_t list)
{
    char* const header = At<char*>(self, list);
    return At<char*>(header, 0x0) == header;
}

int32_t Count(char* self, std::size_t list)
{
    char* const header = At<char*>(self, list);
    int32_t count = 0;
    for (char* node = At<char*>(header, 0x0); node != header; node = At<char*>(node, 0x0))
        ++count;
    return count;
}

void Unlink(char* node)
{
    char* const previous = At<char*>(node, 0x4);
    char* const next = At<char*>(node, 0x0);
    At<char*>(previous, 0x0) = next;
    At<char*>(next, 0x4) = previous;
}

char* NewList()
{
    char* const sentinel = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
    At<char*>(sentinel, 0x0) = sentinel;
    At<char*>(sentinel, 0x4) = sentinel;
    return sentinel;
}

void FreeList(char*& list)
{
    reinterpret_cast<ThisFn>(addr::SC4List_Clear2)(&list);
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, list);
}

void ClearGroups(char* self)
{
    if (!At<uint32_t>(self, field::kGroups + 4))
        return;
    char* const set = self + field::kGroups;
    char* const header = At<char*>(set, 0x0);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Uint32Set_EraseNodes)(set, At<void*>(header, 0x4));
    At<char*>(At<char*>(set, 0x0), 0x8) = At<char*>(set, 0x0);
    At<void*>(At<char*>(set, 0x0), 0x4) = nullptr;
    At<char*>(At<char*>(set, 0x0), 0xC) = At<char*>(set, 0x0);
    At<uint32_t>(set, 0x4) = 0;
}

void PushValue(char* vector, uint32_t value)
{
    auto*& end = At<uint32_t*>(vector, 0x4);
    if (end != At<uint32_t*>(vector, 0x8)) {
        if (end)
            *end = value;
        ++end;
        return;
    }
    uint8_t allocator = 0;
    reinterpret_cast<PushFn>(addr::SC4PtrVector_InsertN)(vector, end, &value, &allocator, 1, 1);
}

// A message of the given type about this instance, posted.
void* NewMessage(char* self, uint32_t type)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x84, self);
    return message;
}

void Post(void* message)
{
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, int32_t{0});
    Release(message);
}

// An effect's placement, as the other disasters'.
struct Placement {
    uint8_t a;
    uint8_t b;
    uint8_t pad[2];
    float matrix[9];
    float position[3];
    float scale;
};

// A cell region (0x24 bytes): the rectangle, then a part at +0x10 whose
// vtable the destructor sets before freeing its cells.
struct CellRegion {
    int32_t rect[4];
    uint8_t cells[0x14];
};

// The occupant's cells inside the city, and the fire cover grid's value
// over them.
uint8_t FireCover(char* self, void* occupant)
{
    int32_t cells[4];
    Virtual<void>(occupant, 0x2C, cells);
    const int32_t city[4] = {0, 0, At<int32_t>(self, field::kCellsX) - 1, At<int32_t>(self, field::kCellsZ) - 1};
    reinterpret_cast<IntersectFn>(addr::SC4Rect_Intersect)(cells, cells, city);
    const int32_t width = cells[2] - cells[0] + 1;
    const int32_t height = cells[3] - cells[1] + 1;
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D78), 0x14);
    return Virtual<uint8_t>(grid, 0x20, cells[0], cells[1], width, height);
}

// The instance's own messages.

// The game's 0x00579400.
void __fastcall SendState(char* self, void*, uint32_t state)
{
    if (At<uint32_t>(self, field::kState) == state)
        return;
    At<uint32_t>(self, field::kState) = state;
    void* const message = NewMessage(self, 0xA9C7CAB6);
    Virtual<void>(message, 0x2C, kFire);
    Virtual<void>(message, 0x40, state);
    Post(message);
}

// The game's 0x00579480.
void __fastcall SendAlert(char* self, void*, uint32_t value)
{
    if (At<uint32_t>(self, field::kAlert) == value)
        return;
    At<uint32_t>(self, field::kAlert) = value;
    void* const message = NewMessage(self, 0x49D15B72);
    Virtual<void>(message, 0x2C, At<uint32_t>(self, field::kAlert));
    Post(message);
}

// The game's 0x00579500.
void __fastcall SendFire(char* self, void*, void* occupant, uint8_t started)
{
    void* const message = NewMessage(self, 0x49E3DB1D);
    Virtual<void>(message, 0x2C, occupant);
    Virtual<void>(message, 0x40, uint32_t{started});
    Post(message);
}

// Identity.

uint32_t __fastcall FireRelease(char* self, void*)
{
    if (At<uint32_t>(self, field::kReferences) == 1)
        Virtual<uint8_t>(self + 0x8, 0x14);
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cRZBaseUnknown_ReleaseCounted)(self);
}

uint8_t __fastcall FireQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x22223BA4)
        *out = self + 0xC;
    else if (iid == 0x452294AA)
        *out = self + 0x10;
    else if (iid == 0x49B43F58)
        *out = self + 0x8;
    else
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(self,
                                                                                                                    iid, out);
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

uint32_t __fastcall SetOwner(char* part, void*, uint32_t value)
{
    uint32_t& owner = At<uint32_t>(part - 0x8, field::kOwner);
    if (!owner)
        owner = value;
    return owner;
}

void* __fastcall FireGetManager(char*, void*)
{
    return Virtual<void*>(Virtual<void*>(City(), 0x120), 0x1C, kFire);
}

uint32_t __fastcall GetFireCount(char* part, void*)
{
    return static_cast<uint32_t>(Count(part - 0x8, field::kFires));
}

// The game's 0x005796B0.
int32_t __fastcall StageFor(char* self, void*, uint32_t heat)
{
    const uint32_t* const limits = At<const uint32_t*>(self, field::kStageLimits);
    for (int32_t stage = 0; stage < 5; ++stage) {
        if (heat < limits[stage])
            return stage;
    }
    return 5;
}

// The game's 0x00579A20.
char* __fastcall Find(char* self, void*, void* occupant)
{
    char* const header = At<char*>(self, field::kFires);
    for (char* node = At<char*>(header, 0x0); node != header; node = At<char*>(node, 0x0)) {
        char* const fire = At<char*>(node, 0x8);
        if (Virtual<void*>(fire, 0x14) == occupant)
            return fire;
    }
    return nullptr;
}

// The game's 0x00579A60.
int32_t __fastcall CountInRect(char* part, void*, const int32_t* rect)
{
    char* const self = part - 0x8;
    if (Empty(self, field::kFires))
        return 0;
    int32_t count = 0;
    char* const header = At<char*>(self, field::kFires);
    for (char* node = At<char*>(header, 0x0); node != header;) {
        char* const fire = At<char*>(node, 0x8);
        node = At<char*>(node, 0x0);
        int32_t cells[4];
        Virtual<void>(fire + 0x4, 0x2C, cells);
        if (rect[2] >= cells[0] && rect[0] <= cells[2] && rect[3] >= cells[1] && rect[1] <= cells[3])
            ++count;
    }
    return count;
}

// The game's 0x00579250 (slot 8).
uint8_t __fastcall ScheduleBurnOut(char* self, void*, char* fire)
{
    if (!fire)
        return 0;
    if (At<uint32_t>(fire, 0x50) == 0) {
        uint32_t when = Below(self, 4000);
        when += Elapsed(self);
        reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(addr::cSC4FireOccupant_CheckBurnOut)(fire, when);
    }
    return 1;
}

// The game's 0x005792B0.
uint8_t __fastcall PickSpreadCell(char* self, void*, char* fire, int32_t* outX, int32_t* outZ)
{
    // As the game's frame: the wind's pair sits right below the bounds.
    struct {
        float wind[2];
        float high[3];
        float low[3];
    } frame;
    Virtual<void>(fire + 0x4, 0x28, frame.low, frame.high);
    const uint32_t stage = Virtual<uint32_t>(fire, 0x18);
    const auto radius = static_cast<float>(static_cast<double>(At<const uint32_t*>(self, field::kRadii)[stage]));
    Virtual<double>(Global<void*>(addr::SC4_Service43DAC), 0x58, frame.wind);
    const double factor = At<float>(self, field::kWind);
    const auto windX = static_cast<float>(static_cast<double>(frame.wind[0]) * factor);
    const auto windZ = static_cast<float>(static_cast<double>(frame.wind[1]) * factor);
    frame.low[0] = static_cast<float>(static_cast<double>(frame.low[0]) - (static_cast<double>(windX) + radius));
    frame.low[2] = static_cast<float>(static_cast<double>(frame.low[2]) - (static_cast<double>(windZ) + radius));
    frame.high[0] = static_cast<float>(static_cast<double>(frame.high[0]) + windX + radius);
    frame.high[2] = static_cast<float>(static_cast<double>(frame.high[2]) + windZ + radius);
    using DrawFn = double(__thiscall*)(void*, double, double);
    const auto Draw = reinterpret_cast<DrawFn>(addr::cRZRandom_UniformSum3);
    const auto x = static_cast<float>(Draw(self + field::kRandom, frame.low[0], frame.high[0]));
    const auto z = static_cast<float>(Draw(self + field::kRandom, frame.low[2], frame.high[2]));
    int32_t cellX = 0;
    int32_t cellZ = 0;
    Virtual<void>(City(), 0x17C, x, z, &cellX, &cellZ);
    *outX = cellX;
    *outZ = cellZ;
    return 1;
}

// The game's 0x00579C10.
void __fastcall FireLoadTuning(char* self, void*)
{
    At<uint32_t>(self, field::kPeriod) = 1000;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (resources)
        Virtual<uint32_t>(resources, 0x4);
    uint32_t key[3] = {0x6534284A, 0x29B6C670, 0xC9B6B73D};
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, uint32_t{0xA52160F5}, &exemplar, int32_t{0}, int32_t{0})) {
        void* const properties = Virtual<void*>(exemplar, 0x1C);
        using VectorFn = uint8_t(__cdecl*)(void*, uint32_t, void*);
        const auto GetVector = reinterpret_cast<VectorFn>(addr::SC4_GetPropertyUint32Vector);
        GetVector(properties, 0xA9B6ECD4, self + field::kSpreadOdds);
        GetVector(properties, 0xA9B6ECD3, self + field::kStageLimits);
        GetVector(properties, 0xA9B6ECD5, self + field::kRadii);
        reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, float*)>(addr::SC4_GetPropertyFloat)(
            properties, 0xA9B6ECD6, &At<float>(self, field::kWind));
        reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyUint8)(
            properties, 0xCA821B8C, &At<uint8_t>(self, field::kZoom));
        using LoadFn = uint8_t(__thiscall*)(void*, const uint32_t*, uint32_t);
        for (uint32_t which = 0; which < 4; ++which)
            reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromExemplar)(self + field::kCurves + 0x10 * which, key,
                                                                     0xA9B6ECD7 + which);
        Release(exemplar);
    } else {
        for (std::size_t vector : {field::kSpreadOdds, field::kStageLimits}) {
            At<uint32_t*>(self, vector + 4) = At<uint32_t*>(self, vector);
            for (int i = 0; i < 5; ++i)
                PushValue(self + vector, 10);
        }
        At<uint32_t*>(self, field::kRadii + 4) = At<uint32_t*>(self, field::kRadii);
        for (uint32_t radius : {0u, 3u, 5u, 7u, 9u})
            PushValue(self + field::kRadii, radius);
        At<float>(self, field::kWind) = 0.0f;
        At<uint8_t>(self, field::kZoom) = 4;
    }
    uint32_t* const limits = At<uint32_t*>(self, field::kStageLimits);
    for (int i = 1; i < 5; ++i)
        limits[i] += limits[i - 1];
    Release(resources);
}

// The game's 0x0057A0D0.
void __fastcall UpdateState(char* self, void*)
{
    void* const target = self + 0x10;
    if (Empty(self, field::kFires)) {
        if (At<uint32_t>(self, field::kState) != 1)
            return;
        SendAlert(self, nullptr, 0);
        SendState(self, nullptr, 3);
        if (void* const layer = Virtual<void*>(City(), 0x120)) {
            Virtual<uint8_t>(layer, 0x38, self + 0xC); // UnregisterDisasterLocations
            Virtual<void>(layer, 0x24);                // DisasterHasEnded
        }
        Virtual<void>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, uint32_t{0x49D15B74}, int32_t{1});
        Virtual<void>(reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)(), 0x18, target, int32_t{0xC0});
        ClearGroups(self);
        return;
    }
    if (At<uint32_t>(self, field::kState) == 1)
        return;
    At<uint32_t>(self, field::kOwner) = 0;
    if (!Global<void*>(addr::SC4_SimulatorService))
        return;
    if (void* const layer = Virtual<void*>(City(), 0x120))
        Virtual<uint32_t>(layer, 0x20, self + 0xC); // DisasterHasBegun
    Virtual<void>(reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)(), 0x14, target, int32_t{0xC0});
    At<uint32_t>(self, field::kNextStep) = Elapsed(self);
    SendState(self, nullptr, 1);
    SendAlert(self, nullptr, 1);
}

// The game's 0x0057A200.
uint8_t __fastcall RemoveAllFires(char* self, void*)
{
    void* const city = City();
    if (!city)
        return 0;
    while (!Empty(self, field::kFires)) {
        char* const node = At<char*>(At<char*>(self, field::kFires), 0x0);
        char* const fire = At<char*>(node, 0x8);
        SendFire(self, nullptr, fire ? fire + 0x4 : nullptr, 0);
        Virtual<void>(Virtual<void*>(city, 0x88), 0x54, fire ? fire + 0x4 : nullptr, int32_t{1}, int32_t{0});
        Unlink(node);
        if (void* const held = At<void*>(node, 0x8))
            Release(held);
        Free(node);
    }
    Virtual<uint8_t>(Virtual<void*>(city, 0x120), 0x38, self + 0xC);
    ClearGroups(self);
    return 0;
}

// The game's 0x005796E0: what the fire leaves of an occupant.
void __fastcall Destroy(char* self, void*, void* occupant)
{
    const char* const* const names = reinterpret_cast<const char* const*>(addr::SC4_FireEffectNames);
    const uint32_t kind = Virtual<uint32_t>(occupant, 0x1C);
    const char* name = nullptr;
    if (kind == kBuilding || kind == kLotType)
        name = names[1];
    else if (kind == kFlora)
        name = names[0];
    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    if (name) {
        void* effect = nullptr;
        Virtual<uint8_t>(effects, 0x1C, name, &effect);
        if (effect) {
            if (Virtual<uint8_t>(effect, 0x24, occupant))
                Virtual<void>(effect, 0xC, int32_t{0});
            if (effect)
                Release(effect);
        }
    }
    uint8_t burning = 0;
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyBool)(
        Virtual<void*>(occupant, 0x18), kBurning, &burning);
    if (burning)
        Virtual<void>(self + 0x8, 0x2C, occupant);
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kFlora) {
        const float start = Global<float>(addr::SC4_FireScorchY);
        float position[3] = {start, start, start};
        Virtual<void>(occupant, 0x20, position);
        void* effect = nullptr;
        if (Virtual<uint8_t>(effects, 0x1C, "fire_flora_scorch", &effect)) {
            Placement placement{};
            placement.matrix[0] = 1.0f;
            placement.matrix[4] = 1.0f;
            placement.matrix[8] = 1.0f;
            placement.scale = 1.0f;
            placement.position[0] = position[0];
            placement.position[1] = position[1];
            placement.position[2] = position[2];
            placement.a = 1;
            placement.b = 1;
            Virtual<void>(effect, 0x20, &placement);
            Virtual<void>(effect, 0xC, int32_t{0});
        }
        Virtual<void>(Global<void*>(addr::SC4_FloraSimulator43D80), 0x5C, occupant);
        if (effect)
            Release(effect);
    } else if (type == kBuilding || type == kLotType) {
        void* const properties = Virtual<void*>(occupant, 0x18);
        uint32_t rubble = 0;
        reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t*)>(addr::SC4_GetPropertyUint32)(properties, 0x6A43150F,
                                                                                                   &rubble);
        uint8_t demolish = 1;
        if (type == kLotType)
            reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyBool)(properties, 0x2A89B677,
                                                                                                     &demolish);
        void* const lot = demolish ? Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant) : nullptr;
        if (lot) {
            int32_t cells[4];
            Virtual<void>(lot, 0x38, cells);
            CellRegion region;
            reinterpret_cast<void*(__thiscall*)(CellRegion*, const int32_t*, uint32_t)>(addr::SC4CellRegion_Construct)(
                &region, cells, 1);
            const uint32_t flags = rubble ? (rubble | 0x300) : 0x300;
            float spot[3];
            Virtual<void>(Global<void*>(addr::SC4_Service43D00), 0x18, int32_t{1}, &region, int32_t{0}, flags, int32_t{0},
                          int32_t{0}, spot, int32_t{0}, int32_t{0}, int32_t{0}, int32_t{0});
            float amount = 0.0f;
            float reach = 0.0f;
            const auto GetFloat = reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, float*)>(addr::SC4_GetPropertyFloat);
            GetFloat(properties, 0xAA5C04C8, &amount);
            GetFloat(properties, 0xAA5C04C5, &reach);
            float position[3];
            Virtual<void>(occupant, 0x20, position);
            if (amount > 0.0f)
                Virtual<void>(Global<void*>(addr::SC4_PollutionSimulator), 0x108, position, reach, amount);
            *reinterpret_cast<std::uintptr_t*>(region.cells) = addr::SC4CellRegion_CellsVtable;
            reinterpret_cast<ThisFn>(addr::SC4CellRegion_DestroyCells)(region.cells);
        } else {
            Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x54, occupant, int32_t{1}, int32_t{0});
        }
    }
    ++At<uint32_t>(self, field::kDestroyed);
}

// The game's 0x0057A720: at most five a call.
void __fastcall ProcessBurned(char* self, void*)
{
    for (int done = 0; !Empty(self, field::kBurned);) {
        char* const node = At<char*>(At<char*>(self, field::kBurned), 0x0);
        void* const occupant = At<void*>(node, 0x8);
        Destroy(self, nullptr, occupant);
        Release(occupant);
        Unlink(node);
        Virtual<uint32_t>(ListNodeAllocator(), 0x10, node);
        if (++done > 4)
            break;
    }
}

// The game's 0x0057AB20.
void __fastcall QueueBurned(char* self, void*, void* occupant)
{
    uint32_t skip = 0;
    if (Count(self, field::kBurned))
        skip = Below(self, static_cast<uint32_t>(Count(self, field::kBurned)));
    char* where = At<char*>(At<char*>(self, field::kBurned), 0x0);
    for (auto left = static_cast<int32_t>(skip); left > 0; --left)
        where = At<char*>(where, 0x0);
    Virtual<uint32_t>(occupant, 0x4);
    char* const node = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
    At<void*>(node, 0x8) = occupant;
    char* const previous = At<char*>(where, 0x4);
    At<char*>(node, 0x0) = where;
    At<char*>(node, 0x4) = previous;
    At<char*>(previous, 0x0) = node;
    At<char*>(where, 0x4) = node;
}

// The game's 0x0057B640.
uint8_t __fastcall RemoveFire(char* self, void*, char* fire)
{
    char* const header = At<char*>(self, field::kFires);
    char* node = At<char*>(header, 0x0);
    for (;; node = At<char*>(node, 0x0)) {
        if (node == header)
            return 0;
        if (At<char*>(node, 0x8) == fire)
            break;
    }
    SendFire(self, nullptr, fire ? fire + 0x4 : nullptr, 0);
    void* const layer = Virtual<void*>(City(), 0x120);
    void* const occupant = Virtual<void*>(fire, 0x14);
    if (Virtual<uint8_t>(layer, 0x3C, self + 0xC, occupant)) {
        uint32_t group = Virtual<uint32_t>(fire, 0x34);
        char* const fires = At<char*>(self, field::kFires);
        char* other = At<char*>(fires, 0x0);
        for (; other != header; other = At<char*>(other, 0x0)) {
            char* const candidate = At<char*>(other, 0x8);
            if (candidate != fire && Virtual<uint32_t>(candidate, 0x34) == group)
                break;
        }
        if (other != header) {
            void* const again = Virtual<void*>(City(), 0x120);
            char* const candidate = At<char*>(other, 0x8);
            const uint32_t zoom = At<uint8_t>(self, field::kZoom);
            void* const burning = Virtual<void*>(candidate, 0x14);
            Virtual<uint8_t>(again, 0x34, self + 0xC, int32_t{0}, int32_t{0}, burning, zoom, int32_t{0});
        } else {
            reinterpret_cast<void(__thiscall*)(void*, uint32_t*)>(addr::SC4Uint32Set_EraseKey)(self + field::kGroups, &group);
        }
    }
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x54, fire + 0x4, int32_t{1}, int32_t{0});
    Unlink(node);
    if (void* const held = At<void*>(node, 0x8))
        Release(held);
    Free(node);
    return 0;
}

// The game's 0x0057B7B0 (slot 9).
uint8_t __fastcall BurnDown(char* self, void*, char* fire)
{
    if (!fire)
        return 0;
    void* const occupant = Virtual<void*>(fire, 0x14);
    if (occupant)
        Virtual<uint32_t>(occupant, 0x4);
    RemoveFire(self, nullptr, fire);
    Destroy(self, nullptr, occupant);
    if (occupant)
        Release(occupant);
    return 1;
}

void __fastcall ScheduleBurnOutOn(char* part, void*, void* occupant)
{
    char* const self = part - 0x8;
    Virtual<uint8_t>(self, 0x20, Find(self, nullptr, occupant));
}

void __fastcall BurnDownOn(char* part, void*, void* occupant)
{
    char* const self = part - 0x8;
    Virtual<uint8_t>(self, 0x24, Find(self, nullptr, occupant));
}

// The game's 0x0057A7D0 (fire interface slot 6).
uint8_t __fastcall StartFire(char* part, void*, void* occupant, uint32_t forcedRaw, uint32_t group, uint32_t jumpRaw)
{
    char* const self = part - 0x8;
    const auto forced = static_cast<uint8_t>(forcedRaw);
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (!forced) {
        void* const simulators = Global<void*>(addr::SC4_SimulatorService);
        if (!Virtual<uint8_t>(simulators, 0x44) && Virtual<uint8_t>(Global<void*>(addr::SC4_SimulatorService), 0x3C))
            return 0;
    }
    if (type != kFlora && type != kLotType && type != kBuilding)
        return 0;
    uint32_t flammability = 0;
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t*)>(addr::SC4_GetPropertyUint32)(
        Virtual<void*>(occupant, 0x18), kFlammability, &flammability);
    if (flammability == 0)
        return 0;
    if (!forced) {
        const uint8_t cover = FireCover(self, occupant);
        if (MagicRound(Evaluate(self, 2, static_cast<float>(static_cast<int32_t>(cover)))) > 0)
            return 0;
    }
    uint8_t burning = 0;
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyBool)(
        Virtual<void*>(occupant, 0x18), kBurning, &burning);
    if (burning) {
        if (!forced)
            return 0;
        if (char* const fire = Find(self, nullptr, occupant))
            Virtual<void>(fire, 0x28, Virtual<int32_t>(fire, 0x24) + 1);
        return 1;
    }
    if (group == 0)
        group = Advance(self);
    void* const memory = OperatorNew(0x6C);
    char* const fire =
        memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4FireOccupant_Construct)(memory) : nullptr;
    char* const groups = self + field::kGroups;
    void* found[2] = {};
    reinterpret_cast<void(__thiscall*)(void*, void**, const uint32_t*)>(addr::SC4Uint32Map_Find)(groups, found, &group);
    if (found[0] == At<void*>(groups, 0x0)) {
        const uint32_t jump = static_cast<uint8_t>(jumpRaw) && At<uint32_t>(groups, 0x4) == 0 ? 1 : 0;
        void* const layer = Virtual<void*>(City(), 0x120);
        Virtual<uint8_t>(layer, 0x34, self + 0xC, int32_t{0}, int32_t{0}, occupant, uint32_t{At<uint8_t>(self, field::kZoom)},
                         jump);
        reinterpret_cast<void(__thiscall*)(void*, void**, const uint32_t*)>(addr::SC4Uint32Set_Insert2)(groups, found,
                                                                                                       &group);
    }
    void* held = fire;
    if (fire)
        Virtual<uint32_t>(fire, 0x4);
    reinterpret_cast<void(__thiscall*)(void*, void* const*)>(addr::SC4RefList_PushBack2)(self + field::kFires, &held);
    if (fire)
        Release(fire);
    char* const fireOccupant = fire + 0x4;
    Virtual<void>(fireOccupant, 0xC);
    Virtual<void>(fire, 0x10, occupant);
    Virtual<void>(fire, 0x38, group);
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x50, fireOccupant, int32_t{0});
    if (forced) {
        const uint32_t stages = Virtual<uint32_t>(fire, 0x1C);
        uint32_t heat = At<const uint32_t*>(self, field::kStageLimits)[0];
        if (stages <= 1)
            heat >>= 1;
        Virtual<void>(fire, 0x28, heat);
    }
    float position[3];
    Virtual<void>(occupant, 0x20, position);
    Virtual<void>(Global<void*>(addr::SC4_Service43D60), 0x18, position[0], position[2], int32_t{0});
    SendFire(self, nullptr, fireOccupant, 1);
    if (Virtual<uint32_t>(occupant, 0x1C) == kFlora)
        ++At<uint32_t>(self, field::kNewFloraFires);
    else if (Virtual<uint32_t>(occupant, 0x1C) != kLotType)
        ++At<uint32_t>(self, field::kNewFires);
    UpdateState(self, nullptr);
    return 1;
}

// The occupants of the three burnable types in a cell rectangle, as a list
// the caller frees.
char* Burnables(char* /*self*/, const int32_t* first, const int32_t* last, void* filter)
{
    char* list = NewList();
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA0, &list, first, last, filter, int32_t{-1});
    return list;
}

// The game's 0x0057AF10 (fire interface slot 10).
uint8_t __fastcall BurnArea(char* part, void*, int32_t x, int32_t z, uint32_t inner, uint32_t outer)
{
    char* const self = part - 0x8;
    uint8_t result = 0;
    const uint32_t group = Advance(self);
    const double cell = Virtual<double>(City(), 0x16C);
    const auto outerReach = static_cast<float>(static_cast<double>(outer) * cell);
    const auto innerReach = static_cast<float>(static_cast<double>(inner) * cell);
    float centre[2];
    Virtual<void>(City(), 0x184, x, z, &centre[0], &centre[1]);
    const auto reach = static_cast<int32_t>(outer);
    int32_t first[2] = {x - reach, z - reach};
    int32_t last[2] = {x + reach, z + reach};
    if (first[0] < 0)
        first[0] = 0;
    if (first[1] < 0)
        first[1] = 0;
    if (static_cast<uint32_t>(last[0]) >= At<uint32_t>(self, field::kCellsX))
        last[0] = At<int32_t>(self, field::kCellsX) - 1;
    if (static_cast<uint32_t>(last[1]) >= At<uint32_t>(self, field::kCellsZ))
        last[1] = At<int32_t>(self, field::kCellsZ) - 1;
    char* list = Burnables(self, first, last, At<void*>(self, field::kFilter));
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        float position[3];
        Virtual<void>(occupant, 0x20, position);
        position[1] = position[2];
        const double distance = reinterpret_cast<double(__cdecl*)(const float*, const float*)>(addr::SC4_Distance2D)(position,
                                                                                                                     centre);
        if (distance < static_cast<double>(innerReach)) {
            uint32_t flammability = 0;
            reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t*)>(addr::SC4_GetPropertyUint32)(
                Virtual<void*>(occupant, 0x18), kFlammability, &flammability);
            if (flammability > 0)
                QueueBurned(self, nullptr, occupant);
            result = 1;
        } else if (distance < static_cast<double>(outerReach)) {
            if (Virtual<uint8_t>(part, 0x18, occupant, int32_t{0}, group, int32_t{0}))
                result = 1;
        }
        Release(occupant);
    }
    FreeList(list);
    return result;
}

// The game's 0x0057B170 (fire interface slot 7).
uint8_t __fastcall StartFireAt(char* part, void*, int32_t x, int32_t z, uint32_t byChance, uint32_t forcedRaw,
                               uint32_t group)
{
    char* const self = part - 0x8;
    uint8_t result = 0;
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D7C), 0x14);
    const uint8_t value = Virtual<uint8_t>(grid, 0x1C, x, z);
    const auto chance = static_cast<float>(Evaluate(self, 3, static_cast<float>(static_cast<int32_t>(value))));
    if (static_cast<uint8_t>(byChance)) {
        using UniformFn = double(__thiscall*)(void*, double, double);
        const double draw = reinterpret_cast<UniformFn>(addr::cRZRandom_Uniform)(self + field::kRandom, 0.0, 100.0);
        if (draw > static_cast<double>(chance))
            return 0;
    }
    if (group == 0)
        group = Advance(self);
    const int32_t cell[2] = {x, z};
    char* list = Burnables(self, cell, cell, At<void*>(self, field::kFilter));
    const uint32_t jump = (byChance & 0xFFFFFF00) | (static_cast<uint8_t>(forcedRaw) == 0 ? 1u : 0u);
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        if (Virtual<uint8_t>(part, 0x18, occupant, forcedRaw, group, jump))
            result = 1;
        Release(occupant);
    }
    FreeList(list);
    return result;
}

// The game's 0x0057B350 (fire interface slot 8).
uint8_t __fastcall DouseAt(char* part, void*, int32_t x, int32_t z, uint32_t amount)
{
    char* const self = part - 0x8;
    struct {
        std::uintptr_t vtable;
        std::uintptr_t second;
        uint32_t references;
        uint32_t rest;
    } filter{};
    char* list = NewList();
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4_OccupantFilterBase)(&filter);
    filter.vtable = addr::SC4OccupantFilter43_vftable;
    filter.second = addr::SC4OccupantFilter43_vftable - 0x18;
    ++filter.references;
    const int32_t cell[2] = {x, z};
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA0, &list, cell, cell, &filter, int32_t{-1});
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        char* fire = nullptr;
        Virtual<uint8_t>(occupant, 0x0, uint32_t{0x69D25C60}, &fire);
        if (fire) {
            const uint32_t heat = Virtual<uint32_t>(fire, 0x24);
            if (heat <= amount) {
                Virtual<void>(fire, 0x28, int32_t{0});
                Virtual<uint8_t>(self, 0x20, fire);
            } else {
                Virtual<void>(fire, 0x28, heat - amount);
            }
        }
        Release(occupant);
        if (fire)
            Release(fire);
    }
    filter.vtable = addr::SC4OccupantFilterBase_vftable;
    filter.second = addr::SC4OccupantFilterBase_vftable - 0x18;
    FreeList(list);
    return 1;
}

// The game's 0x0057B500 (fire interface slot 9).
uint8_t __fastcall BurnDownAt(char* part, void*, int32_t x, int32_t z)
{
    char* const self = part - 0x8;
    uint8_t result = 0;
    const int32_t cell[2] = {x, z};
    char* list = Burnables(self, cell, cell, At<void*>(self, field::kFilter));
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        result = 1;
        QueueBurned(self, nullptr, occupant);
        Release(occupant);
    }
    FreeList(list);
    return result;
}

// The game's 0x0057BB90: one fire's step.
void __fastcall BurnStep(char* self, void*, char* fire)
{
    void* const occupant = Virtual<void*>(fire, 0x14);
    if (occupant)
        Virtual<uint32_t>(occupant, 0x4);
    if (!Virtual<uint8_t>(occupant, 0x80, int32_t{1})) {
        RemoveFire(self, nullptr, fire);
        Release(occupant);
        return;
    }
    if (At<uint32_t>(fire, 0x50) > 0) {
        const uint32_t now = Elapsed(self);
        if (!reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(addr::cSC4FireOccupant_CheckBurnOut)(fire, now))
            RemoveFire(self, nullptr, fire);
        Release(occupant);
        return;
    }
    const uint8_t cover = FireCover(self, occupant);
    int32_t change = 1;
    int32_t cooling = MagicRound(Evaluate(self, 2, static_cast<float>(static_cast<int32_t>(cover))));
    const uint32_t responders = Virtual<uint32_t>(fire, 0x2C);
    if (responders > 0)
        cooling += static_cast<int32_t>(responders * 4);
    if (cooling > 0)
        change = -static_cast<int32_t>(Below(self, static_cast<uint32_t>(cooling + 1)));
    bool grew = false;
    int32_t heat = Virtual<int32_t>(fire, 0x24);
    if (heat < 20 && heat + change >= 20)
        grew = true;
    heat += change;
    if (heat < 0) {
        Virtual<void>(fire, 0x28, int32_t{0});
        Virtual<uint8_t>(self, 0x20, fire);
        Release(occupant);
        return;
    }
    Virtual<void>(fire, 0x28, heat);
    const auto stage = static_cast<uint32_t>(StageFor(self, nullptr, static_cast<uint32_t>(heat)));
    const uint32_t stages = Virtual<uint32_t>(fire, 0x1C);
    if (stage != Virtual<uint32_t>(fire, 0x18)) {
        if (stage >= stages) {
            if (!At<uint8_t>(self, field::kHold))
                Virtual<uint8_t>(self, 0x24, fire);
            Release(occupant);
            return;
        }
        Virtual<void>(fire, 0x20, stage);
        if (heat >= 20)
            grew = true;
    }
    const uint32_t type = Virtual<uint32_t>(Virtual<void*>(fire, 0x14), 0x1C);
    if (type == kBuilding || (type == kLotType && grew)) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        if (void* const lot = Virtual<void*>(lots, 0x38, Virtual<void*>(fire, 0x14))) {
            if (stage >= stages - 1)
                Virtual<void>(lot, 0x90, int32_t{6});
            else if (stage >= stages - 2)
                Virtual<void>(lot, 0x90, int32_t{5});
        }
    }
    const double odds =
        static_cast<double>(1.0f) / static_cast<double>(At<const uint32_t*>(self, field::kSpreadOdds)[stage]);
    using UniformFn = double(__thiscall*)(void*, double, double);
    const double draw = reinterpret_cast<UniformFn>(addr::cRZRandom_Uniform)(self + field::kRandom, 0.0, 1.0);
    if (draw < odds) {
        int32_t x = 0;
        int32_t z = 0;
        PickSpreadCell(self, nullptr, fire, &x, &z);
        if (x >= 0 && x < Virtual<int32_t>(City(), 0x174) && z >= 0 && z < Virtual<int32_t>(City(), 0x178)) {
            const uint32_t group = Virtual<uint32_t>(fire, 0x34);
            Virtual<uint8_t>(self + 0x8, 0x1C, x, z, int32_t{1}, int32_t{0}, group);
        }
    }
    Release(occupant);
}

// The game's 0x0057BEA0.
void __fastcall SpreadAll(char* self, void*)
{
    void* const simulator = Virtual<void*>(City(), 0x148);
    At<uint8_t>(self, field::kHold) = simulator && Virtual<uint8_t>(simulator, 0x20) ? 1 : 0;
    char* const header = At<char*>(self, field::kFires);
    for (char* node = At<char*>(header, 0x0); node != header;) {
        char* const fire = At<char*>(node, 0x8);
        node = At<char*>(node, 0x0);
        if (!Virtual<void*>(fire, 0x14)) {
            RemoveAllFires(self, nullptr);
            return;
        }
        BurnStep(self, nullptr, fire);
    }
}

// The game's 0x0057BF10 (fire interface slot 4).
void __fastcall Update(char* part, void*)
{
    char* const self = part - 0x8;
    ProcessBurned(self, nullptr);
    UpdateState(self, nullptr);
    if (!Empty(self, field::kFires)) {
        const uint32_t now = Elapsed(self);
        uint32_t& next = At<uint32_t>(self, field::kNextStep);
        if (!next)
            next = now;
        if (now >= next) {
            SpreadAll(self, nullptr);
            At<uint32_t>(self, field::kNextStep) += At<uint32_t>(self, field::kPeriod);
        }
    }
    if (!At<uint32_t>(self, field::kNewFloraFires) && !At<uint32_t>(self, field::kNewFires))
        return;
    void* const message = NewMessage(self, 0x49D15B73);
    Virtual<void>(message, 0x2C, At<uint32_t>(self, field::kNewFires));
    Virtual<void>(message, 0x40, At<uint32_t>(self, field::kNewFloraFires));
    Post(message);
    At<uint32_t>(self, field::kNewFloraFires) = 0;
    At<uint32_t>(self, field::kNewFires) = 0;
}

// The game's 0x0057A310 (fire interface slot 3).
uint8_t __fastcall FireInit(char* part, void*)
{
    char* const self = part - 0x8;
    if (At<uint8_t>(self, field::kInited))
        return 1;
    FireLoadTuning(self, nullptr);
    At<uint8_t>(self, field::kInited) = 1;
    if (void* const layer = Virtual<void*>(City(), 0x120)) {
        if (void* const manager = Virtual<void*>(layer, 0x1C, kFire))
            Virtual<uint32_t>(manager, 0x20, self + 0xC); // AddInstanceToList
    }
    Global<void*>(addr::SC4_FireSpreadCurves + 0x4) = self + field::kCurves + 0x10;
    Global<void*>(addr::SC4_FireSpreadCurves) = self + field::kCurves;
    At<int32_t>(self, field::kCellsX) = Virtual<int32_t>(City(), 0x174);
    At<int32_t>(self, field::kCellsZ) = Virtual<int32_t>(City(), 0x178);
    if (!At<void*>(self, field::kLots)) {
        void* const lots = Global<void*>(addr::SC4_Service43D00);
        At<void*>(self, field::kLots) = lots;
        Virtual<uint32_t>(lots, 0x4);
    }
    reinterpret_cast<void(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_ConstructMode)(self + field::kStopwatch, 4);
    reinterpret_cast<ThisFn>(addr::GZStopwatch_Start)(self + field::kStopwatch);
    char* const filter = static_cast<char*>(OperatorNew(0x18));
    if (filter) {
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4_OccupantFilterBase)(filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4OccupantTypeFilter_vftable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4OccupantTypeFilter_vftable_4;
        At<uint32_t>(filter, 0xC) = 0;
        At<uint32_t>(filter, 0x10) = 0;
        At<uint32_t>(filter, 0x14) = 0;
    }
    void* const old = At<void*>(self, field::kFilter);
    if (filter != old) {
        if (filter)
            Virtual<uint32_t>(filter, 0x4);
        At<void*>(self, field::kFilter) = filter;
        if (old)
            Release(old);
    }
    for (uint32_t type : {kFlora, kBuilding, kLotType})
        PushValue(At<char*>(self, field::kFilter) + 0xC, type);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<void>(server, 0x14, self + 0x10, uint32_t{0x06CDB65B});
        Virtual<void>(server, 0x14, self + 0x10, uint32_t{0xA6B79602});
    }
    Virtual<void>(part, 0x10);
    return 1;
}

// The game's 0x0057A540 (fire interface slot 5).
uint8_t __fastcall FireShutdown(char* part, void*)
{
    char* const self = part - 0x8;
    if (!At<uint8_t>(self, field::kInited))
        return 1;
    Virtual<uint8_t>(Virtual<void*>(City(), 0x120), 0x38, self + 0xC);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<void>(server, 0x18, self + 0x10, uint32_t{0xA6B79602});
        Virtual<void>(server, 0x18, self + 0x10, uint32_t{0x06CDB65B});
    }
    Virtual<void>(Virtual<void*>(City(), 0xC8), 0x50, self + 0x10);
    if (void* const filter = At<void*>(self, field::kFilter)) {
        At<void*>(self, field::kFilter) = nullptr;
        Release(filter);
    }
    RemoveAllFires(self, nullptr);
    reinterpret_cast<ThisFn>(addr::SC4RefList_Clear)(self + field::kFires);
    ClearGroups(self);
    while (!Empty(self, field::kBurned)) {
        char* const node = At<char*>(At<char*>(self, field::kBurned), 0x0);
        Release(At<void*>(node, 0x8));
        Unlink(node);
        Virtual<uint32_t>(ListNodeAllocator(), 0x10, node);
    }
    Global<void*>(addr::SC4_FireSpreadCurves) = nullptr;
    Global<void*>(addr::SC4_FireSpreadCurves + 0x4) = nullptr;
    At<uint8_t>(self, field::kInited) = 0;
    if (void* const lots = At<void*>(self, field::kLots)) {
        Release(lots);
        At<void*>(self, field::kLots) = nullptr;
    }
    if (void* const layer = Virtual<void*>(City(), 0x120)) {
        if (void* const manager = Virtual<void*>(layer, 0x1C, kFire))
            Virtual<uint8_t>(manager, 0x24, self + 0xC); // RemoveInstanceFromList
    }
    return 1;
}

uint8_t __fastcall FireDoMessage(char* target, void*, void* message)
{
    char* const self = target - 0x10;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x86AD10EE || type == 0x66956813) {
        Virtual<void>(self + 0x8, 0x10);
    } else if (type == 0x06CDB65B) {
        const uint32_t flag = Virtual<int32_t>(Global<void*>(addr::SC4_Service43DC4), 0x58) == 1 ? 1 : 0;
        char* const header = At<char*>(self, field::kFires);
        for (char* node = At<char*>(header, 0x0); node != header;) {
            char* const fire = At<char*>(node, 0x8);
            node = At<char*>(node, 0x0);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4FireOccupant_SetView)(fire, flag);
        }
    } else if (type == 0xA6B79602) {
        char* const header = At<char*>(self, field::kFires);
        for (char* node = At<char*>(header, 0x0); node != header;) {
            char* const fire = At<char*>(node, 0x8);
            node = At<char*>(node, 0x0);
            reinterpret_cast<ThisFn>(addr::cSC4FireOccupant_Refresh)(fire);
        }
    }
    return 1;
}

// The manager (`interface` at +8, the fire part at +0x24).

uint8_t __fastcall FireManagerQueryInterface(char* manager, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == 0x49B43F57) {
        void* const extra = manager ? manager + 0x24 : nullptr;
        *out = extra;
        if (extra) {
            Virtual<uint32_t>(manager, 0x4);
            return 1;
        }
    }
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cSC4DisasterManager_QueryInterface)(
        manager, iid, out);
}

// A new instance, initialised: its fire interface with a reference.
char* MakeInstance()
{
    void* const memory = OperatorNew(0x100);
    char* const made =
        memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4DisasterInstanceFire_Construct)(memory) : nullptr;
    char* const part = made + 0x8;
    Virtual<uint8_t>(part, 0xC);
    Virtual<uint32_t>(made, 0x4);
    return part;
}

// The instance's fire interface from the manager's fire part, made at
// first use.
char* InstanceFor(char* extra)
{
    char* const list = At<char*>(extra - 0x10, 0x0);
    if (At<char*>(list, 0x0) == list)
        return MakeInstance();
    return Virtual<char*>(extra, 0x2C);
}

char* __fastcall GetOrMakeInstance(char* manager, void*)
{
    return InstanceFor(manager + 0x24);
}

char* __fastcall FireManagerGetInstance(char* extra, void*)
{
    char* const list = At<char*>(extra - 0x10, 0x0);
    if (At<char*>(list, 0x0) == list)
        return nullptr;
    char* part = nullptr;
    Virtual<uint8_t>(At<void*>(At<char*>(list, 0x0), 0x8), 0x0, uint32_t{0x49B43F58}, &part);
    return part;
}

uint8_t __fastcall FireManagerInit(char* interface, void*)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Init)(interface);
    if (char* const part = InstanceFor(interface + 0x1C)) {
        Virtual<uint8_t>(part, 0xC);
        Release(part);
    }
    return 1;
}

void* __fastcall FireAttemptDisasterStart(char* interface, void*, int32_t x, int32_t z)
{
    Virtual<uint8_t>(interface + 0x1C, 0x14, x, z, int32_t{0}, int32_t{0}, int32_t{0});
    return nullptr;
}

uint32_t __fastcall FireManagerSetOwner(char* extra, void*, uint32_t value)
{
    char* const part = InstanceFor(extra);
    if (!part)
        return 0;
    const uint32_t owner = Virtual<uint32_t>(part, 0x34, value);
    Release(part);
    return owner;
}

uint32_t __fastcall FireManagerGetOwner(char* extra, void*)
{
    char* const part = InstanceFor(extra);
    if (!part)
        return 0;
    const uint32_t owner = Virtual<uint32_t>(part, 0x38);
    Release(part);
    return owner;
}

uint8_t __fastcall FireManagerIgnite(char* extra, void*, void* occupant)
{
    if (!occupant)
        return 0;
    char* const part = InstanceFor(extra);
    if (!part)
        return 0;
    Virtual<uint8_t>(part, 0x18, occupant, int32_t{0}, int32_t{0}, int32_t{1});
    Release(part);
    return 1;
}

uint8_t __fastcall FireManagerStartFireAt(char* extra, void*, uint32_t x, uint32_t z, uint32_t byChance, uint32_t forced,
                                          uint32_t group)
{
    char* const part = InstanceFor(extra);
    if (!part)
        return 0;
    Virtual<uint8_t>(part, 0x1C, x, z, byChance, forced, group);
    Release(part);
    return 1;
}

uint8_t __fastcall FireManagerDouseAt(char* extra, void*, uint32_t x, uint32_t z, uint32_t amount)
{
    uint8_t result = 0;
    if (char* const part = Virtual<char*>(extra, 0x2C)) {
        result = Virtual<uint8_t>(part, 0x20, x, z, amount);
        Release(part);
    }
    return result;
}

uint8_t __fastcall FireManagerBurnDownAt(char* extra, void*, uint32_t x, uint32_t z)
{
    char* const part = InstanceFor(extra);
    if (!part)
        return 0;
    Virtual<uint8_t>(part, 0x24, x, z);
    Release(part);
    return 1;
}

uint8_t __fastcall FireManagerBurnArea(char* extra, void*, uint32_t x, uint32_t z, uint32_t inner, uint32_t outer)
{
    char* const part = InstanceFor(extra);
    if (!part)
        return 0;
    Virtual<uint8_t>(part, 0x28, x, z, inner, outer);
    Release(part);
    return 1;
}

bool InstallFire(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[56] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterManagerFire_QueryInterface, reinterpret_cast<void*>(&FireManagerQueryInterface)},
        {addr::cSC4DisasterManagerFire_DouseAt, reinterpret_cast<void*>(&FireManagerDouseAt)},
        {addr::cSC4DisasterManagerFire_AttemptDisasterStart, reinterpret_cast<void*>(&FireAttemptDisasterStart)},
        {addr::cSC4DisasterInstanceFire_Release, reinterpret_cast<void*>(&FireRelease)},
        {addr::cSC4DisasterInstanceFire_QueryInterface, reinterpret_cast<void*>(&FireQueryInterface)},
        {addr::cSC4DisasterInstanceFire_SetOwner, reinterpret_cast<void*>(&SetOwner)},
        {addr::cSC4DisasterInstanceFire_ScheduleBurnOut, reinterpret_cast<void*>(&ScheduleBurnOut)},
        {addr::cSC4DisasterInstanceFire_PickSpreadCell, reinterpret_cast<void*>(&PickSpreadCell)},
        {addr::cSC4DisasterInstanceFire_GetManager, reinterpret_cast<void*>(&FireGetManager)},
        {addr::cSC4DisasterInstanceFire_SendState, reinterpret_cast<void*>(&SendState)},
        {addr::cSC4DisasterInstanceFire_SendAlert, reinterpret_cast<void*>(&SendAlert)},
        {addr::cSC4DisasterInstanceFire_SendFire, reinterpret_cast<void*>(&SendFire)},
        {addr::cSC4DisasterInstanceFire_DoMessage, reinterpret_cast<void*>(&FireDoMessage)},
        {addr::cSC4DisasterInstanceFire_StageFor, reinterpret_cast<void*>(&StageFor)},
        {addr::cSC4DisasterInstanceFire_Destroy, reinterpret_cast<void*>(&Destroy)},
        {addr::cSC4DisasterInstanceFire_Find, reinterpret_cast<void*>(&Find)},
        {addr::cSC4DisasterInstanceFire_CountInRect, reinterpret_cast<void*>(&CountInRect)},
        {addr::cSC4DisasterInstanceFire_ScheduleBurnOutOn, reinterpret_cast<void*>(&ScheduleBurnOutOn)},
        {addr::cSC4DisasterInstanceFire_BurnDownOn, reinterpret_cast<void*>(&BurnDownOn)},
        {addr::cSC4DisasterManagerFire_GetInstance, reinterpret_cast<void*>(&FireManagerGetInstance)},
        {addr::cSC4DisasterInstanceFire_LoadTuning, reinterpret_cast<void*>(&FireLoadTuning)},
        {addr::cSC4DisasterInstanceFire_UpdateState, reinterpret_cast<void*>(&UpdateState)},
        {addr::cSC4DisasterInstanceFire_RemoveAllFires, reinterpret_cast<void*>(&RemoveAllFires)},
        {addr::cSC4DisasterInstanceFire_GetFireCount, reinterpret_cast<void*>(&GetFireCount)},
        {addr::cSC4DisasterInstanceFire_Init, reinterpret_cast<void*>(&FireInit)},
        {addr::cSC4DisasterInstanceFire_Shutdown, reinterpret_cast<void*>(&FireShutdown)},
        {addr::cSC4DisasterInstanceFire_ProcessBurned, reinterpret_cast<void*>(&ProcessBurned)},
        {addr::cSC4DisasterInstanceFire_StartFire, reinterpret_cast<void*>(&StartFire)},
        {addr::cSC4DisasterInstanceFire_QueueBurned, reinterpret_cast<void*>(&QueueBurned)},
        {addr::cSC4DisasterInstanceFire_BurnArea, reinterpret_cast<void*>(&BurnArea)},
        {addr::cSC4DisasterInstanceFire_StartFireAt, reinterpret_cast<void*>(&StartFireAt)},
        {addr::cSC4DisasterInstanceFire_DouseAt, reinterpret_cast<void*>(&DouseAt)},
        {addr::cSC4DisasterInstanceFire_BurnDownAt, reinterpret_cast<void*>(&BurnDownAt)},
        {addr::cSC4DisasterInstanceFire_RemoveFire, reinterpret_cast<void*>(&RemoveFire)},
        {addr::cSC4DisasterInstanceFire_BurnDown, reinterpret_cast<void*>(&BurnDown)},
        {addr::cSC4DisasterManagerFire_GetOrMakeInstance, reinterpret_cast<void*>(&GetOrMakeInstance)},
        {addr::cSC4DisasterManagerFire_SetOwner, reinterpret_cast<void*>(&FireManagerSetOwner)},
        {addr::cSC4DisasterManagerFire_GetOwner, reinterpret_cast<void*>(&FireManagerGetOwner)},
        {addr::cSC4DisasterManagerFire_Ignite, reinterpret_cast<void*>(&FireManagerIgnite)},
        {addr::cSC4DisasterManagerFire_StartFireAt, reinterpret_cast<void*>(&FireManagerStartFireAt)},
        {addr::cSC4DisasterManagerFire_BurnDownAt, reinterpret_cast<void*>(&FireManagerBurnDownAt)},
        {addr::cSC4DisasterManagerFire_BurnArea, reinterpret_cast<void*>(&FireManagerBurnArea)},
        {addr::cSC4DisasterManagerFire_Init, reinterpret_cast<void*>(&FireManagerInit)},
        {addr::cSC4DisasterInstanceFire_BurnStep, reinterpret_cast<void*>(&BurnStep)},
        {addr::cSC4DisasterInstanceFire_SpreadAll, reinterpret_cast<void*>(&SpreadAll)},
        {addr::cSC4DisasterInstanceFire_Update, reinterpret_cast<void*>(&Update)},
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

const bool g_fireRegistered = registry::Register({
    "simulators.disasters.fire",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterInstanceFire_Update,
    "Our fire disaster manager and instance (docs/disasters.md)",
    false,
    InstallFire,
});

} // namespace
} // namespace opensc4::disasters
