// The pipe burst (docs/disasters.md): cSC4DisasterManagerPipeBurst (class
// 0x4A132FDF, disaster 0x4A1332E8) and cSC4DisasterInstancePipeBurst (class
// 0x0A133012): bursts at water pipes, each with a fuse; when it runs out the
// pipe explodes and the bursts spread to the pipes it connects to.
// Replacement, off by default:
//
//   simulators.disasters.pipeburst: the manager (Init, Shutdown, the start
//     at a cell or anywhere, making an instance), the pipe filter, and the
//     instance (Init, Shutdown, the tuning, bursts, effects, the tick, the
//     messages).
//
// The objects' constructors and destructors and the list and vector helpers
// stay the game's.
#include "core/address_check.h"
#include "core/fixed_seed.h"
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

constexpr uint32_t kPipeBurst = 0x4A1332E8;
constexpr uint32_t kPipeType = 0xC772BF98;
constexpr uint32_t kPipeInterface = 0x49C05B9F;

namespace field { // the instance
constexpr std::size_t kInited = 0x10;
constexpr std::size_t kManager = 0x14;
constexpr std::size_t kGround = 0x18;   // city slot 0x150: craters
constexpr std::size_t kEffects = 0x1C;  // city slot 0x154: effects
constexpr std::size_t kFilter = 0x20;
constexpr std::size_t kViewMode = 0x24; // 1 above ground, 3 underground
constexpr std::size_t kBursts = 0x28;   // list of Burst
constexpr std::size_t kWidth = 0x2C;
constexpr std::size_t kDepth = 0x30;
constexpr std::size_t kSpreads = 0x34;  // list of Spread
} // namespace field

struct Burst {
    float position[3];
    int32_t x;
    int32_t z;
    uint32_t time;
    uint32_t fuse;
    void* effect;
};
static_assert(sizeof(Burst) == 0x20);

struct BurstItem {
    BurstItem* next;
    BurstItem* previous;
    Burst burst;
};

struct Cells {
    int32_t* begin;
    int32_t* end;
    int32_t* capacity;
};

struct SpreadItem {
    SpreadItem* next;
    SpreadItem* previous;
    int32_t age;
    Cells cells;
};

using ThisFn = void(__thiscall*)(void* self);

void ReleaseObject(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void* City()
{
    return Global<void*>(addr::SC4_City);
}

// A pipe filter (0x10 bytes): the game's base and vtables, the plumbing
// simulator at +0xC.
char* NewFilter()
{
    auto* const filter = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x10));
    if (filter) {
        reinterpret_cast<ThisFn>(addr::SC4_OccupantFilterBase)(filter);
        At<std::uintptr_t>(filter, 0x0) = addr::cSC4PipeBurstFilter_vftable;
        At<std::uintptr_t>(filter, 0x4) = addr::cSC4PipeBurstFilter_vftable_4;
        At<void*>(filter, 0xC) = nullptr;
    }
    return filter;
}

void FillFilter(char* filter)
{
    void* const plumbing = Virtual<void*>(City(), 0x100);
    At<void*>(filter, 0xC) = plumbing;
    if (plumbing)
        Virtual<uint32_t>(plumbing, 0x4);
}

void DropFilter(char* owner, std::size_t at)
{
    char* const filter = At<char*>(owner, at);
    if (!filter)
        return;
    if (void* const plumbing = At<void*>(filter, 0xC)) {
        Virtual<uint32_t>(plumbing, 0x8);
        At<void*>(filter, 0xC) = nullptr;
    }
    Virtual<uint32_t>(At<char*>(owner, at), 0x8);
    At<char*>(owner, at) = nullptr;
}

// The pipe at a cell that the filter accepts, as its pipe interface.
char* PipeAt(char* filter, int32_t x, int32_t z)
{
    void* found = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x80, &found, x, z, filter))
        return nullptr;
    char* pipe = nullptr;
    Virtual<uint8_t>(found, 0x0, kPipeInterface, &pipe);
    Virtual<uint32_t>(found, 0x8);
    return pipe;
}

// The filter's slot 3.
uint8_t __fastcall FilterAccepts(char* filter, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kPipeType)
        return 0;
    void* pipe = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kPipeInterface, &pipe))
        return 0;
    int32_t x;
    int32_t z;
    Virtual<void>(pipe, 0xEC, &x, &z);
    const uint8_t result = Virtual<uint8_t>(At<void*>(filter, 0xC), 0x2C, x, z);
    Virtual<uint32_t>(pipe, 0x8);
    return result;
}

// The manager (`interface` is at +8; the filter at +0x24).

uint8_t __fastcall PipeManagerInit(char* interface, void*)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Init)(interface);
    if (!At<char*>(interface, 0x1C)) {
        char* const filter = NewFilter();
        At<char*>(interface, 0x1C) = filter;
        Virtual<uint32_t>(filter, 0x4);
        FillFilter(At<char*>(interface, 0x1C));
    }
    return 1;
}

uint8_t __fastcall PipeManagerShutdown(char* interface, void*)
{
    DropFilter(interface, 0x1C);
    reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Shutdown)(interface);
    return 1;
}

// The game's 0x0057CF90: an instance for the pipe occupant; its interface,
// or null when it could not start.
char* __fastcall CreateInstance(char*, void*, void* occupant)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x38);
    char* made = memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4DisasterInstancePipeBurst_Construct)(memory)
                        : nullptr;
    Virtual<uint32_t>(made, 0x4);
    void* pipe = nullptr;
    Virtual<uint8_t>(occupant, 0x0, kPipeInterface, &pipe);
    const uint8_t started =
        reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4DisasterInstancePipeBurst_Init)(made, pipe);
    Virtual<uint32_t>(made, 0x8);
    if (!started)
        made = nullptr;
    Virtual<uint32_t>(pipe, 0x8);
    return made ? made + 0x8 : nullptr;
}

// An instance at the cell's accepted pipe; false when there is none there.
bool StartAt(char* interface, int32_t x, int32_t z, char** made)
{
    void* occupant = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x80, &occupant, x, z, At<char*>(interface, 0x1C)))
        return false;
    *made = reinterpret_cast<char*(__thiscall*)(void*, void*)>(addr::cSC4DisasterManagerPipeBurst_CreateInstance)(
        interface - 0x8, occupant);
    Virtual<uint32_t>(occupant, 0x8);
    return true;
}

char* __fastcall PipeAttemptDisasterStart(char* interface, void*, int32_t x, int32_t z)
{
    char* made = nullptr;
    StartAt(interface, x, z, &made);
    return made;
}

// From a random cell outwards in a spiral to the first accepted pipe.
char* __fastcall PipeAttemptDisasterStartAnywhere(char* interface, void*)
{
    const int32_t width = Virtual<int32_t>(City(), 0x174);
    const int32_t depth = Virtual<int32_t>(City(), 0x178);
    alignas(8) char random[0x18] = {};
    {
        // The clock seed counts under the original's call site.
        const fixed_seed::SiteOverride site(0x0057D072);
        reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Random_Construct)(random, -1);
    }
    using BelowFn = int32_t(__thiscall*)(void*, int32_t);
    int32_t x = reinterpret_cast<BelowFn>(addr::cRZRandom_Below)(random, width);
    int32_t z = reinterpret_cast<BelowFn>(addr::cRZRandom_Below)(random, depth);
    alignas(8) char walk[0x30] = {};
    using StartFn = void(__thiscall*)(void*, int32_t, int32_t);
    At<int32_t>(walk, 0x10) = INT32_MIN;
    At<int32_t>(walk, 0x14) = INT32_MIN;
    At<int32_t>(walk, 0x18) = INT32_MAX;
    At<int32_t>(walk, 0x1C) = INT32_MAX;
    At<uint8_t>(walk, 0x20) = 1;
    At<int32_t>(walk, 0x2C) = 0;
    reinterpret_cast<StartFn>(addr::SC4SpiralWalk_Start)(walk, 0, 0);
    At<int32_t>(walk, 0x10) = 0;
    At<int32_t>(walk, 0x14) = 0;
    At<int32_t>(walk, 0x18) = width - 1 < 0 ? 0 : width - 1;
    At<int32_t>(walk, 0x1C) = depth - 1 < 0 ? 0 : depth - 1;
    reinterpret_cast<StartFn>(addr::SC4SpiralWalk_Start)(walk, x, z);
    while (reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t*, int32_t*)>(addr::SC4SpiralWalk_Next)(walk, &x, &z)) {
        char* made = nullptr;
        if (StartAt(interface, x, z, &made))
            return made;
    }
    return nullptr;
}

// The instance.

// The game's 0x0057C390.
void __fastcall MakeEffect(char* pipeBurst, void*, const char* name, const float* position, uint8_t keep, void** out)
{
    *out = nullptr;
    if (!Virtual<uint8_t>(At<void*>(pipeBurst, field::kEffects), 0x1C, name, out))
        return;
    struct {
        uint8_t a;
        uint8_t b;
        uint8_t pad[2];
        float matrix[9];
        float position[3];
        float scale;
    } placement{};
    placement.a = 1;
    placement.b = 1;
    placement.matrix[0] = 1.0f;
    placement.matrix[4] = 1.0f;
    placement.matrix[8] = 1.0f;
    std::memcpy(placement.position, position, sizeof placement.position);
    placement.scale = 1.0f;
    Virtual<void>(*out, 0x20, &placement);
    Virtual<void>(*out, 0xC, int32_t{0});
    if (!keep) {
        Virtual<uint32_t>(*out, 0x8);
        *out = nullptr;
    }
}

void StopEffect(Burst* burst)
{
    if (!burst->effect)
        return;
    Virtual<void>(burst->effect, 0x10, int32_t{1});
    Virtual<uint32_t>(burst->effect, 0x8);
    burst->effect = nullptr;
}

// The game's 0x0057C5B0.
void __fastcall UpdateBurstEffect(char* pipeBurst, void*, Burst* burst)
{
    StopEffect(burst);
    const int32_t mode = At<int32_t>(pipeBurst, field::kViewMode);
    const auto make = reinterpret_cast<void(__thiscall*)(void*, const char*, const float*, uint8_t, void**)>(
        addr::cSC4DisasterInstancePipeBurst_MakeEffect);
    if (mode == 1)
        make(pipeBurst, "burst_pipe_aboveground", burst->position, 1, &burst->effect);
    else if (mode == 3)
        make(pipeBurst, "burst_pipe_underground", burst->position, 1, &burst->effect);
    burst->time = 0;
    const int32_t* const range = reinterpret_cast<const int32_t*>(addr::cSC4DisasterInstancePipeBurst_FuseRange);
    burst->fuse = reinterpret_cast<uint32_t(__thiscall*)(void*, int32_t, int32_t)>(addr::cRZRandom_Between)(
        reinterpret_cast<void*>(addr::cSC4DisasterInstancePipeBurst_Random), range[0], range[1]);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, int32_t, int32_t, int32_t)>(addr::cSC4DisasterLayer_Post)(
        pipeBurst, 0xEA1F0DCD, burst->x, burst->z, 0);
}

void __fastcall OnViewMode(char* pipeBurst, void*, const int32_t* mode)
{
    if (*mode == At<int32_t>(pipeBurst, field::kViewMode))
        return;
    At<int32_t>(pipeBurst, field::kViewMode) = *mode;
    BurstItem* const list = At<BurstItem*>(pipeBurst, field::kBursts);
    for (BurstItem* item = list->next; item != list; item = item->next)
        reinterpret_cast<void(__thiscall*)(void*, Burst*)>(addr::cSC4DisasterInstancePipeBurst_UpdateBurstEffect)(
            pipeBurst, &item->burst);
}

void __fastcall RemoveBurstAt(char* pipeBurst, void*, int32_t x, int32_t z)
{
    BurstItem* const list = At<BurstItem*>(pipeBurst, field::kBursts);
    for (BurstItem* item = list->next; item != list; item = item->next) {
        if (item->burst.x != x || item->burst.z != z)
            continue;
        StopEffect(&item->burst);
        item->previous->next = item->next;
        item->next->previous = item->previous;
        Free(item);
        return;
    }
}

void __fastcall OnOccupantRemoved(char* pipeBurst, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kPipeType)
        return;
    void* pipe = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kPipeInterface, &pipe))
        return;
    int32_t x;
    int32_t z;
    Virtual<void>(pipe, 0xEC, &x, &z);
    reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t)>(addr::cSC4DisasterInstancePipeBurst_RemoveBurstAt)(
        pipeBurst, x, z);
    Virtual<uint32_t>(pipe, 0x8);
}

// The game's 0x0057C7C0.
uint8_t __fastcall AddBurst(char* pipeBurst, void*, char* pipe)
{
    int32_t x;
    int32_t z;
    Virtual<void>(pipe, 0xEC, &x, &z);
    BurstItem* const list = At<BurstItem*>(pipeBurst, field::kBursts);
    for (BurstItem* item = list->next; item != list; item = item->next) {
        if (item->burst.x == x && item->burst.z == z)
            return 0;
    }
    Burst fresh{};
    const float start = Global<float>(addr::cSC4DisasterInstancePipeBurst_Statics + 4);
    fresh.position[0] = start;
    fresh.position[1] = start;
    fresh.position[2] = start;
    BurstItem* inserted;
    reinterpret_cast<void(__thiscall*)(void*, BurstItem**, BurstItem*, const Burst*)>(addr::cSC4PipeBurstList_Insert)(
        pipeBurst + field::kBursts, &inserted, At<BurstItem*>(pipeBurst, field::kBursts), &fresh);
    Burst* const burst = &At<BurstItem*>(pipeBurst, field::kBursts)->previous->burst;
    Virtual<void>(pipe + 0x4, 0x20, burst->position);
    burst->x = x;
    burst->z = z;
    burst->effect = nullptr;
    reinterpret_cast<void(__thiscall*)(void*, Burst*)>(addr::cSC4DisasterInstancePipeBurst_UpdateBurstEffect)(pipeBurst,
                                                                                                             burst);
    return 1;
}

void __fastcall SpreadTo(char* pipeBurst, void*, const Cells* cells)
{
    for (const int32_t* cell = cells->begin; cell != cells->end; cell += 2) {
        if (char* const pipe = PipeAt(At<char*>(pipeBurst, field::kFilter), cell[0], cell[1])) {
            reinterpret_cast<uint8_t(__thiscall*)(void*, char*)>(addr::cSC4DisasterInstancePipeBurst_AddBurst)(pipeBurst,
                                                                                                             pipe);
            Virtual<uint32_t>(pipe, 0x8);
        }
    }
}

void __fastcall AgeSpreads(char* pipeBurst, void*)
{
    for (SpreadItem* item = At<SpreadItem*>(pipeBurst, field::kSpreads)->next;
         item != At<SpreadItem*>(pipeBurst, field::kSpreads);) {
        if (++item->age <= 3) {
            item = item->next;
            continue;
        }
        reinterpret_cast<void(__thiscall*)(void*, const Cells*)>(addr::cSC4DisasterInstancePipeBurst_SpreadTo)(
            pipeBurst, &item->cells);
        SpreadItem* const next = item->next;
        item->previous->next = next;
        next->previous = item->previous;
        if (item->cells.begin)
            Free(item->cells.begin);
        Free(item);
        item = next;
    }
}

// The game's 0x0057C1A0.
void __fastcall Crater(char* pipeBurst, void*, int32_t x, int32_t z)
{
    void* const ground = At<void*>(pipeBurst, field::kGround);
    const double cell = static_cast<double>(Virtual<float>(ground, 0x18));
    struct {
        float x;
        float z;
        float scratch[2];
        uint32_t zero;
        uint8_t flag;
        uint8_t pad[3];
        float one[2];
        uint32_t three;
        uint32_t unset[4];
        float hundred;
    } request{};
    static_assert(sizeof request == 0x38);
    request.x = static_cast<float>(static_cast<double>(x) * cell);
    request.z = static_cast<float>(static_cast<double>(z) * cell);
    request.scratch[0] = request.x;
    request.scratch[1] = request.z;
    request.flag = 1;
    request.one[0] = 1.0f;
    request.one[1] = 1.0f;
    request.three = 3;
    request.hundred = 100.0f;
    Virtual<void>(At<void*>(pipeBurst, field::kGround), 0xE4, int32_t{0x14}, &request);
}

// The game's 0x0057CB90: the pipe at the cell explodes.
void __fastcall BurstPipe(char* pipeBurst, void*, int32_t x, int32_t z)
{
    char* const pipe = PipeAt(At<char*>(pipeBurst, field::kFilter), x, z);
    if (!pipe)
        return;
    const float start = Global<float>(addr::cSC4DisasterInstancePipeBurst_Statics + 4);
    float position[3] = {start, start, start};
    Virtual<void>(pipe + 0x4, 0x20, position);
    void* effect;
    reinterpret_cast<void(__thiscall*)(void*, const char*, const float*, uint8_t, void**)>(
        addr::cSC4DisasterInstancePipeBurst_MakeEffect)(pipeBurst, "pipe_explodes", position, 0, &effect);
    Cells cells{};
    static const int32_t kDx[] = {-1, 0, 1, 0};
    static const int32_t kDz[] = {0, -1, 0, 1};
    for (int32_t side = 0; side < 4; ++side) {
        if (!Virtual<uint8_t>(pipe, 0xBC, side, int32_t{4}))
            continue;
        int32_t cell[2] = {x + kDx[side], z + kDz[side]};
        if (cell[0] < 0 || cell[0] >= At<int32_t>(pipeBurst, field::kWidth) || cell[1] < 0 ||
            cell[1] >= At<int32_t>(pipeBurst, field::kDepth))
            continue;
        if (cells.end != cells.capacity) {
            if (cells.end) {
                cells.end[0] = cell[0];
                cells.end[1] = cell[1];
            }
            cells.end += 2;
        } else {
            uint8_t tag;
            reinterpret_cast<void(__thiscall*)(void*, int32_t*, const int32_t*, uint8_t*, uint32_t, uint32_t)>(
                addr::SC4PointVector_InsertN)(&cells, cells.end, cell, &tag, 1, 1);
        }
    }
    if (cells.begin != cells.end) {
        SpreadItem* const list = At<SpreadItem*>(pipeBurst, field::kSpreads);
        auto* const item =
            static_cast<SpreadItem*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0x18));
        item->age = 0;
        reinterpret_cast<void(__thiscall*)(void*, const Cells*)>(addr::SC4PointVector_CopyConstruct)(&item->cells, &cells);
        SpreadItem* const last = list->previous;
        item->next = list;
        item->previous = last;
        last->next = item;
        list->previous = item;
    }
    if (void* const lots = Global<void*>(addr::SC4_Service43D00)) {
        Virtual<void>(lots, 0x20, int32_t{1}, pipe + 0x4, int32_t{0}, int32_t{0x302}, int32_t{0}, int32_t{0}, int32_t{0});
        reinterpret_cast<void(__thiscall*)(void*, uint32_t, int32_t, int32_t, int32_t)>(addr::cSC4DisasterLayer_Post)(
            pipeBurst, 0xEA1F0DCD, x, z, 0);
    }
    Virtual<uint32_t>(pipe, 0x8);
    reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t)>(addr::cSC4DisasterInstancePipeBurst_Crater)(pipeBurst, x,
                                                                                                           z);
    if (cells.begin)
        Free(cells.begin);
}

// The game's 0x0057CD70.
void __fastcall PipeTick(char* pipeBurst, void*, uint32_t passed)
{
    reinterpret_cast<ThisFn>(addr::cSC4DisasterInstancePipeBurst_AgeSpreads)(pipeBurst);
    for (BurstItem* item = At<BurstItem*>(pipeBurst, field::kBursts)->next;
         item != At<BurstItem*>(pipeBurst, field::kBursts);) {
        item->burst.time += passed;
        if (!item->burst.effect || item->burst.time < item->burst.fuse) {
            item = item->next;
            continue;
        }
        reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t)>(addr::cSC4DisasterInstancePipeBurst_Burst)(
            pipeBurst, item->burst.x, item->burst.z);
        Virtual<void>(item->burst.effect, 0x10, int32_t{0});
        BurstItem* const next = item->next;
        item->previous->next = next;
        next->previous = item->previous;
        Free(item);
        item = next;
    }
    BurstItem* const bursts = At<BurstItem*>(pipeBurst, field::kBursts);
    SpreadItem* const spreads = At<SpreadItem*>(pipeBurst, field::kSpreads);
    if (bursts->next != bursts || spreads->next != spreads)
        return;
    char* const interface = pipeBurst + 0x8;
    if (void* const layer = Virtual<void*>(City(), 0x120)) {
        Virtual<uint8_t>(layer, 0x38, interface); // UnregisterDisasterLocations
        Virtual<void>(layer, 0x24);               // DisasterHasEnded
    }
    Virtual<uint8_t>(interface, 0xC); // Shutdown
}

uint8_t __fastcall PipeDoMessage(char* target, void*, void* message)
{
    char* const pipeBurst = target - 0xC;
    const auto removed = reinterpret_cast<void(__thiscall*)(void*, void*)>(
        addr::cSC4DisasterInstancePipeBurst_OnOccupantRemoved);
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x99EF1142: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, uint32_t{0x029244DB}))
            removed(pipeBurst, occupant);
        break;
    }
    case 0x86AD10EE:
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4DisasterInstancePipeBurst_Tick)(
            pipeBurst, Virtual<uint32_t>(message, 0x38));
        break;
    case 0x99EF1143:
        removed(pipeBurst, Virtual<void*>(message, 0x28));
        break;
    case 0xC6CDBF1A:
        reinterpret_cast<void(__thiscall*)(void*, const int32_t*)>(addr::cSC4DisasterInstancePipeBurst_OnViewMode)(
            pipeBurst, Virtual<const int32_t*>(message, 0x3C));
        break;
    default:
        break;
    }
    return 1;
}

// The game's 0x0057C220.
void __cdecl LoadTuning()
{
    uint32_t key[3] = {0x6534284A, 0x29B6C670, 3};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, uint32_t{0xA52160F5}, &exemplar, int32_t{0},
                          int32_t{0}))
        return;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    using FloatFn = uint8_t(__cdecl*)(void*, uint32_t, float*);
    const double scale = static_cast<double>(Global<float>(addr::SC4_Float867AC));
    int32_t* const range = reinterpret_cast<int32_t*>(addr::cSC4DisasterInstancePipeBurst_FuseRange);
    float seconds;
    reinterpret_cast<FloatFn>(addr::SC4_GetPropertyFloat)(properties, 0x4A08C600, &seconds);
    range[0] = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(seconds) * scale));
    reinterpret_cast<FloatFn>(addr::SC4_GetPropertyFloat)(properties, 0x4A08C601, &seconds);
    range[1] = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(seconds) * scale));
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyUint8)(
        properties, 0xCA821B8C, reinterpret_cast<uint8_t*>(addr::cSC4DisasterInstancePipeBurst_Statics + 1));
    Virtual<uint32_t>(exemplar, 0x8);
}

// The game's 0x0057C880.
uint8_t __fastcall PipeInit(char* pipeBurst, void*, char* pipe)
{
    uint8_t& loaded = Global<uint8_t>(addr::cSC4DisasterInstancePipeBurst_Statics);
    if (!loaded) {
        loaded = 1;
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                      reinterpret_cast<void*>(addr::cSC4DisasterInstancePipeBurst_ResetStatics), int32_t{0});
        reinterpret_cast<void(__cdecl*)()>(addr::cSC4DisasterInstancePipeBurst_LoadTuning)();
    }
    At<uint8_t>(pipeBurst, field::kInited) = 1;
    void* const city = City();
    At<int32_t>(pipeBurst, field::kWidth) = Virtual<int32_t>(city, 0x174);
    At<int32_t>(pipeBurst, field::kDepth) = Virtual<int32_t>(city, 0x178);
    char* const filter = NewFilter();
    At<char*>(pipeBurst, field::kFilter) = filter;
    Virtual<uint32_t>(filter, 0x4);
    FillFilter(At<char*>(pipeBurst, field::kFilter));
    void* const layer = Virtual<void*>(city, 0x120);
    if (char* const manager = Virtual<char*>(layer, 0x1C, kPipeBurst)) {
        At<char*>(pipeBurst, field::kManager) = manager - 0x8;
        Virtual<uint32_t>(manager - 0x8, 0x4);
    }
    char* const interface = pipeBurst + 0x8;
    Virtual<uint32_t>(At<char*>(pipeBurst, field::kManager) + 0x8, 0x20, interface); // AddInstanceToList
    for (const auto& [at, slot] : {std::pair<std::size_t, std::size_t>{field::kGround, 0x150},
                                   std::pair<std::size_t, std::size_t>{field::kEffects, 0x154}}) {
        void* const object = Virtual<void*>(city, slot);
        At<void*>(pipeBurst, at) = object;
        if (object)
            Virtual<uint32_t>(object, 0x4);
    }
    char* const target = pipeBurst + 0xC;
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (uint32_t type : {0xC6CDBF1Au, 0x99EF1142u, 0x99EF1143u})
            Virtual<void>(server, 0x14, target, type);
    }
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x14, target, int32_t{0xC0});
    void* const view = Global<void*>(addr::SC4_Service43DC4);
    At<int32_t>(pipeBurst, field::kViewMode) = view ? Virtual<int32_t>(view, 0x58) : 1;
    if (!reinterpret_cast<uint8_t(__thiscall*)(void*, char*)>(addr::cSC4DisasterInstancePipeBurst_AddBurst)(pipeBurst,
                                                                                                          pipe)) {
        Virtual<uint8_t>(interface, 0xC);
        return 0;
    }
    const uint32_t zoom = Global<uint8_t>(addr::cSC4DisasterInstancePipeBurst_Statics + 1);
    Virtual<uint8_t>(layer, 0x34, interface, int32_t{0}, int32_t{0}, pipe ? pipe + 0x4 : nullptr, zoom, int32_t{1});
    Virtual<uint32_t>(layer, 0x20, interface); // DisasterHasBegun
    return 1;
}

uint8_t __fastcall PipeShutdown(char* interface, void*)
{
    char* const pipeBurst = interface - 0x8;
    if (!At<uint8_t>(pipeBurst, field::kInited))
        return 1;
    At<uint8_t>(pipeBurst, field::kInited) = 0;
    BurstItem* const list = At<BurstItem*>(pipeBurst, field::kBursts);
    for (BurstItem* item = list->next; item != list; item = item->next)
        StopEffect(&item->burst);
    for (BurstItem* item = At<BurstItem*>(pipeBurst, field::kBursts)->next;
         item != At<BurstItem*>(pipeBurst, field::kBursts);) {
        BurstItem* const next = item->next;
        Free(item);
        item = next;
    }
    At<BurstItem*>(pipeBurst, field::kBursts)->next = At<BurstItem*>(pipeBurst, field::kBursts);
    At<BurstItem*>(pipeBurst, field::kBursts)->previous = At<BurstItem*>(pipeBurst, field::kBursts);
    char* const target = pipeBurst + 0xC;
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x18, target, int32_t{0xC0});
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (uint32_t type : {0xC6CDBF1Au, 0x99EF1142u, 0x99EF1143u})
            Virtual<void>(server, 0x18, target, type);
    }
    for (std::size_t at : {field::kEffects, field::kGround}) {
        if (void* const object = At<void*>(pipeBurst, at)) {
            Virtual<uint32_t>(object, 0x8);
            At<void*>(pipeBurst, at) = nullptr;
        }
    }
    DropFilter(pipeBurst, field::kFilter);
    Virtual<uint8_t>(At<char*>(pipeBurst, field::kManager) + 0x8, 0x24, interface); // RemoveInstanceFromList
    if (char* const manager = At<char*>(pipeBurst, field::kManager)) {
        Virtual<uint32_t>(manager, 0x8);
        At<char*>(pipeBurst, field::kManager) = nullptr;
    }
    return 1;
}

bool InstallPipeBurst(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4PipeBurstFilter_Accepts, reinterpret_cast<void*>(&FilterAccepts)},
        {addr::cSC4DisasterManagerPipeBurst_Shutdown, reinterpret_cast<void*>(&PipeManagerShutdown)},
        {addr::cSC4DisasterInstancePipeBurst_Crater, reinterpret_cast<void*>(&Crater)},
        {addr::cSC4DisasterInstancePipeBurst_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::cSC4DisasterManagerPipeBurst_Init, reinterpret_cast<void*>(&PipeManagerInit)},
        {addr::cSC4DisasterInstancePipeBurst_MakeEffect, reinterpret_cast<void*>(&MakeEffect)},
        {addr::cSC4DisasterInstancePipeBurst_Shutdown, reinterpret_cast<void*>(&PipeShutdown)},
        {addr::cSC4DisasterInstancePipeBurst_UpdateBurstEffect, reinterpret_cast<void*>(&UpdateBurstEffect)},
        {addr::cSC4DisasterInstancePipeBurst_OnViewMode, reinterpret_cast<void*>(&OnViewMode)},
        {addr::cSC4DisasterInstancePipeBurst_RemoveBurstAt, reinterpret_cast<void*>(&RemoveBurstAt)},
        {addr::cSC4DisasterInstancePipeBurst_OnOccupantRemoved, reinterpret_cast<void*>(&OnOccupantRemoved)},
        {addr::cSC4DisasterInstancePipeBurst_AddBurst, reinterpret_cast<void*>(&AddBurst)},
        {addr::cSC4DisasterInstancePipeBurst_Init, reinterpret_cast<void*>(&PipeInit)},
        {addr::cSC4DisasterInstancePipeBurst_SpreadTo, reinterpret_cast<void*>(&SpreadTo)},
        {addr::cSC4DisasterInstancePipeBurst_AgeSpreads, reinterpret_cast<void*>(&AgeSpreads)},
        {addr::cSC4DisasterInstancePipeBurst_Burst, reinterpret_cast<void*>(&BurstPipe)},
        {addr::cSC4DisasterInstancePipeBurst_Tick, reinterpret_cast<void*>(&PipeTick)},
        {addr::cSC4DisasterInstancePipeBurst_DoMessage, reinterpret_cast<void*>(&PipeDoMessage)},
        {addr::cSC4DisasterManagerPipeBurst_CreateInstance, reinterpret_cast<void*>(&CreateInstance)},
        {addr::cSC4DisasterManagerPipeBurst_AttemptDisasterStartAnywhere,
         reinterpret_cast<void*>(&PipeAttemptDisasterStartAnywhere)},
        {addr::cSC4DisasterManagerPipeBurst_AttemptDisasterStart, reinterpret_cast<void*>(&PipeAttemptDisasterStart)},
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

const bool g_pipeBurstRegistered = registry::Register({
    "simulators.disasters.pipeburst",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterInstancePipeBurst_Tick,
    "Our pipe burst manager and instance (docs/disasters.md)",
    false,
    InstallPipeBurst,
});

} // namespace
} // namespace opensc4::disasters
