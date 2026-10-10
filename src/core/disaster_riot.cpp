// The riot (docs/disasters.md): cSC4DisasterManagerRiot (class 0x4A38206C,
// disaster 0xCA380B06) and cSC4DisasterInstanceRiot (class 0x4A38206D): a
// list of rioter automata whose anger grows where the police cover is thin,
// level by level, spawning more rioters. Replacement, off by default:
//
//   simulators.disasters.riot: the manager (Init, Shutdown, the tuning, the
//     start at a cell, the tracked lots) and the instance (Init, Shutdown,
//     the tuning, adding a rioter, the tick, the phase, the animation, the
//     messages, Write).
//
// The instance's Read (0x0057E870), the objects' constructors and
// destructors and the list, set and vector helpers stay the game's.
// `riot` is the instance object: its interface is at +8, the serializable
// at +0xC and the message target at +0x10.
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

constexpr uint32_t kRiot = 0xCA380B06;

namespace field { // the instance
constexpr std::size_t kWidth = 0x14;
constexpr std::size_t kDepth = 0x18;
constexpr std::size_t kRioters = 0x1C;  // list of Rioter
constexpr std::size_t kInited = 0x20;
constexpr std::size_t kManager = 0x24;
constexpr std::size_t kPhase = 0x28;    // 0 none yet, 1 rioting, 3 over
constexpr std::size_t kStopwatch = 0x30;
constexpr std::size_t kNextTick = 0x48;
constexpr std::size_t kLevels = 0x4C;
constexpr std::size_t kLevelTime = 0x50;
constexpr std::size_t kTuning54 = 0x54;
constexpr std::size_t kSpawns = 0x58;   // vector: rioters spawned on reaching a level
constexpr std::size_t kCurve = 0x64;    // police cover -> calm per tick
constexpr std::size_t kZoom = 0x74;
} // namespace field

struct Rioter {
    void* automaton;
    int32_t time;
    int32_t level;
    int32_t topLevel;
    void* lot;
};

struct RioterItem {
    RioterItem* next;
    RioterItem* previous;
    Rioter rioter;
};
static_assert(sizeof(RioterItem) == 0x1C);

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

using ThisFn = void(__thiscall*)(void* self);

void* City()
{
    return Global<void*>(addr::SC4_City);
}

void NewString(GameString& text)
{
    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
}

void FreeString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text.begin);
}

template <typename... Args>
void Format(GameString& text, const char* format, Args... args)
{
    reinterpret_cast<void(__cdecl*)(GameString*, const char*, ...)>(addr::cRZBaseString_Sprintf)(&text, format, args...);
}

int32_t Count(RioterItem* list)
{
    int32_t count = 0;
    for (RioterItem* item = list->next; item != list; item = item->next)
        ++count;
    return count;
}

// A spiral walk's state (0x30 bytes), bounded to the city.
void StartWalk(char* walk, int32_t x, int32_t z)
{
    using StartFn = void(__thiscall*)(void*, int32_t, int32_t);
    At<int32_t>(walk, 0x10) = INT32_MIN;
    At<int32_t>(walk, 0x14) = INT32_MIN;
    At<int32_t>(walk, 0x18) = INT32_MAX;
    At<int32_t>(walk, 0x1C) = INT32_MAX;
    At<uint8_t>(walk, 0x20) = 1;
    At<int32_t>(walk, 0x2C) = 0;
    reinterpret_cast<StartFn>(addr::SC4SpiralWalk_Start)(walk, 0, 0);
    const int32_t depth = Virtual<int32_t>(City(), 0x178) - 1;
    const int32_t width = Virtual<int32_t>(City(), 0x174) - 1;
    At<int32_t>(walk, 0x10) = 0;
    At<int32_t>(walk, 0x14) = 0;
    At<int32_t>(walk, 0x18) = width < 0 ? 0 : width;
    At<int32_t>(walk, 0x1C) = depth < 0 ? 0 : depth;
    if (At<uint8_t>(walk, 0x28) != 1)
        At<uint8_t>(walk, 0x21) = 1;
    reinterpret_cast<StartFn>(addr::SC4SpiralWalk_Start)(walk, x, z);
}

// The manager (`interface` at +8; the set of tracked lots at +0x24).

void __fastcall RiotManagerLoadTuning(char*, void*)
{
    uint32_t key[3] = {0x6534284A, 0x29B6C670, 0x4A38206D};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, uint32_t{0xA52160F5}, &exemplar, int32_t{0},
                          int32_t{0}))
        return;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    using Uint32Fn = uint8_t(__cdecl*)(void*, uint32_t, uint32_t*);
    uint32_t* const limits = reinterpret_cast<uint32_t*>(addr::cSC4DisasterManagerRiot_Limits);
    reinterpret_cast<Uint32Fn>(addr::SC4_GetPropertyUint32)(properties, 0xCADB6B3E, &limits[0]);
    reinterpret_cast<Uint32Fn>(addr::SC4_GetPropertyUint32)(properties, 0x2ADB6B6D, &limits[1]);
    Virtual<uint32_t>(exemplar, 0x8);
}

uint8_t __fastcall RiotManagerInit(char* interface, void*)
{
    const uint8_t result = reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Init)(interface);
    if (result)
        reinterpret_cast<ThisFn>(addr::cSC4DisasterManagerRiot_LoadTuning)(interface - 0x8);
    return result;
}

struct SetNode { // std::set node
    uint8_t color;
    uint8_t pad[3];
    SetNode* parent;
    SetNode* left;
    SetNode* right;
    void* key;
};

SetNode* NextNode(SetNode* node)
{
    if (SetNode* right = node->right) {
        while (right->left)
            right = right->left;
        return right;
    }
    SetNode* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return node->right == parent ? node : parent;
}

// The game's 0x0057D720.
void __fastcall ClearTracked(char* manager, void*)
{
    char* const set = manager + 0x24;
    SetNode* const header = At<SetNode*>(set, 0x0);
    for (SetNode* node = header->left; node != At<SetNode*>(set, 0x0); node = NextNode(node)) {
        void* const lot = node->key;
        Virtual<void>(Virtual<void*>(lot, 0xC0), 0x30, int32_t{2});
        Virtual<uint32_t>(lot, 0x8);
    }
    if (At<uint32_t>(set, 0x4)) {
        reinterpret_cast<void(__thiscall*)(void*, SetNode*)>(addr::SC4OccupantSet_EraseTree)(set,
                                                                                            At<SetNode*>(set, 0x0)->parent);
        At<SetNode*>(set, 0x0)->left = At<SetNode*>(set, 0x0);
        At<SetNode*>(set, 0x0)->parent = nullptr;
        At<SetNode*>(set, 0x0)->right = At<SetNode*>(set, 0x0);
        At<uint32_t>(set, 0x4) = 0;
    }
}

uint8_t __fastcall RiotManagerShutdown(char* interface, void*)
{
    if (At<uint8_t>(interface, 0x8))
        reinterpret_cast<ThisFn>(addr::cSC4DisasterManagerRiot_ClearTracked)(interface - 0x8);
    return reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterManager_Shutdown)(interface);
}

uint8_t __fastcall RiotRemoveInstanceFromList(char* interface, void*, void* instance)
{
    const uint8_t result = reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(
        addr::cSC4DisasterManager_RemoveInstanceFromList)(interface, instance);
    char* const list = At<char*>(interface, 0xC);
    if (At<char*>(list, 0x0) == list)
        reinterpret_cast<ThisFn>(addr::cSC4DisasterManagerRiot_ClearTracked)(interface - 0x8);
    return result;
}

// Main vtable slot 9.
void __fastcall Track(char* manager, void*, void* lot)
{
    if (!lot)
        return;
    char* const set = manager + 0x24;
    SetNode* found;
    reinterpret_cast<void(__thiscall*)(void*, SetNode**, void* const*)>(addr::SC4Uint32Map_Find)(set, &found, &lot);
    if (found != At<SetNode*>(set, 0x0))
        return;
    Virtual<uint32_t>(lot, 0x4);
    Virtual<void>(Virtual<void*>(lot, 0xC0), 0x34, int32_t{2});
    uint32_t result[2];
    reinterpret_cast<void(__thiscall*)(void*, uint32_t*, void* const*)>(addr::SC4PtrSet_Insert)(set, result, &lot);
}

void* __fastcall RiotAttemptDisasterStart(char* interface, void*, int32_t x, int32_t z)
{
    if (Virtual<uint32_t>(interface, 0x28) >= Global<uint32_t>(addr::cSC4DisasterManagerRiot_Limits))
        return nullptr;
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x78);
    char* const riot = memory ? reinterpret_cast<char*(__thiscall*)(void*)>(addr::cSC4DisasterInstanceRiot_Construct)(memory)
                              : nullptr;
    if (riot)
        Virtual<uint32_t>(riot, 0x4);
    Virtual<uint32_t>(interface, 0x20, riot ? riot + 0x8 : nullptr);
    reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4DisasterInstanceRiot_Init)(riot);
    reinterpret_cast<uint32_t(__thiscall*)(void*, int32_t, int32_t, uint8_t)>(addr::cSC4DisasterInstanceRiot_AddRioter)(
        riot, x, z, 0);
    if (riot)
        Virtual<uint32_t>(riot, 0x8);
    return nullptr;
}

// The instance.

uint8_t __fastcall RiotQueryInterface(char* riot, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    std::size_t at;
    if (iid == 0x22223BA4)
        at = 0x8;
    else if (iid == 0xE4FDA3D4)
        at = 0xC;
    else
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            riot, iid, out);
    *out = riot ? riot + at : nullptr;
    Virtual<uint32_t>(riot, 0x4);
    return 1;
}

// The game's 0x0057D300 (its first argument is not used).
void __fastcall Agitate(char* riot, void*, void*, uint32_t x, uint32_t z)
{
    char* const manager = At<char*>(riot, field::kManager);
    void* const lots = Virtual<void*>(City(), 0xA0);
    for (uint32_t cx = x - 3; cx <= x + 3 && x - 3 <= x + 3; ++cx) {
        for (uint32_t cz = z - 3; cz <= z + 3 && z - 3 <= z + 3; ++cz) {
            void** const lot = Virtual<void**>(lots, 0x38, cx, cz);
            if (lot && *lot)
                Virtual<void>(manager, 0x24, *lot); // Track
        }
    }
}

// The game's 0x0057D390.
void __fastcall UpdatePhase(char* riot, void*)
{
    RioterItem* const list = At<RioterItem*>(riot, field::kRioters);
    const int32_t phase = At<int32_t>(riot, field::kPhase);
    if (phase == 0) {
        if (list->next == list)
            return;
        At<int32_t>(riot, field::kPhase) = 1;
        if (void* const layer = Virtual<void*>(City(), 0x120))
            Virtual<uint32_t>(layer, 0x20, riot + 0x8); // DisasterHasBegun
        Virtual<void>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, uint32_t{0xAA8C87C7}, int32_t{1});
    } else if (phase == 1) {
        if (list->next != list)
            return;
        At<int32_t>(riot, field::kPhase) = 3;
        if (void* const layer = Virtual<void*>(City(), 0x120)) {
            Virtual<uint8_t>(layer, 0x38, riot + 0x8); // UnregisterDisasterLocations
            Virtual<void>(layer, 0x24);                // DisasterHasEnded
        }
    }
}

uint8_t __fastcall RiotShutdown(char* interface, void*)
{
    char* const riot = interface - 0x8;
    if (!At<uint8_t>(riot, field::kInited))
        return 1;
    At<uint8_t>(riot, field::kInited) = 0;
    Virtual<uint8_t>(Virtual<void*>(City(), 0x120), 0x38, interface);
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x18, riot + 0x10, int32_t{0xC0});
    Virtual<uint8_t>(At<char*>(riot, field::kManager) + 0x8, 0x24, interface); // RemoveInstanceFromList
    if (char* const manager = At<char*>(riot, field::kManager)) {
        Virtual<uint32_t>(manager, 0x8);
        At<char*>(riot, field::kManager) = nullptr;
    }
    return 1;
}

// The game's 0x0057D4C0.
void* __fastcall FindLotNear(char*, void*, int32_t* x, int32_t* z)
{
    int32_t cx = *x;
    int32_t cz = *z;
    void* const lots = Virtual<void*>(City(), 0xA0);
    alignas(8) char walk[0x30] = {};
    StartWalk(walk, cx, cz);
    while (At<int32_t>(walk, 0x2C) < 8) {
        if (void** const lot = Virtual<void**>(lots, 0x38, cx, cz)) {
            void* const found = *lot;
            if (Virtual<uint8_t>(found, 0x58, int32_t{0}) || Virtual<uint8_t>(found, 0x58, int32_t{3}) ||
                Virtual<uint8_t>(found, 0x58, int32_t{6})) {
                *x = cx;
                *z = cz;
                return found;
            }
        }
        reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t*, int32_t*)>(addr::SC4SpiralWalk_Next)(walk, &cx, &cz);
    }
    return nullptr;
}

// The game's 0x0057DC20.
void __fastcall SetAnimation(char* riot, void*, void* automaton, int32_t level)
{
    GameString name{};
    NewString(name);
    for (uint32_t i = 0; i < At<uint32_t>(riot, field::kLevels); ++i) {
        Format(name, "Riot%d", i + 1);
        Virtual<void>(automaton, 0xAC, &name);
    }
    if (level >= 0 && static_cast<uint32_t>(level) < At<uint32_t>(riot, field::kLevels)) {
        Format(name, "Riot%d", level + 1);
        Virtual<void>(automaton, 0xA4, &name, &name);
    }
    FreeString(name);
}

// What the police curve gives for the cover at a cell.
double Calm(char* riot, void* police, int32_t x, int32_t z)
{
    const int32_t cover = Virtual<uint8_t>(police, 0x1C, x, z);
    return static_cast<double>(reinterpret_cast<float(__thiscall*)(void*, float)>(addr::SC4Curve_Evaluate)(
        riot + field::kCurve, static_cast<float>(cover)));
}

// The game's 0x0057DD00.
uint32_t __fastcall AddRioter(char* riot, void*, int32_t x, int32_t z, uint8_t spawned)
{
    void* const lot = reinterpret_cast<void*(__thiscall*)(void*, int32_t*, int32_t*)>(
        addr::cSC4DisasterInstanceRiot_FindLotNear)(riot, &x, &z);
    if (!lot)
        return 0;
    void* const police = Virtual<void*>(Global<void*>(addr::SC4_PoliceSimulator), 0x14);
    if (static_cast<int32_t>(static_cast<int64_t>(Calm(riot, police, x, z))) > 1)
        return 0;
    RioterItem* const list = At<RioterItem*>(riot, field::kRioters);
    if (static_cast<uint32_t>(Count(list)) >= Global<uint32_t>(addr::cSC4DisasterManagerRiot_Limits + 4))
        return 0;
    void* const automata = Virtual<void*>(City(), 0xB0);
    const float start = Global<float>(addr::cSC4DisasterInstanceRiot_Statics + 4);
    float position[3] = {start, start, start};
    Virtual<void>(City(), 0x180, x, z, &position[0], &position[2]);
    void* const random = reinterpret_cast<void*>(addr::cSC4DisasterInstanceRiot_Random);
    using NextFn = double(__thiscall*)(void*);
    position[0] = static_cast<float>(reinterpret_cast<NextFn>(addr::cRZRandom_NextFloat)(random) * 16.0 +
                                     static_cast<double>(position[0]));
    position[2] = static_cast<float>(reinterpret_cast<NextFn>(addr::cRZRandom_NextFloat)(random) * 16.0 +
                                     static_cast<double>(position[2]));
    position[1] = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, position[0], position[2]);
    float facing[3] = {0, 0, 0};
    void* const automaton = Virtual<void*>(automata, 0x3C, int32_t{0x4107}, position, int32_t{2}, facing, int32_t{0},
                                           int32_t{0xFF}, int32_t{0});
    if (automaton) {
        Rioter fresh{};
        Virtual<uint32_t>(automaton, 0x4);
        fresh.level = -1;
        fresh.topLevel = -1;
        fresh.automaton = automaton;
        fresh.time = 0;
        reinterpret_cast<void(__thiscall*)(void**, void*)>(addr::SC4RefPtr_Assign)(&fresh.lot, lot);
        RioterItem* inserted;
        reinterpret_cast<void(__thiscall*)(void*, RioterItem**, RioterItem*, const Rioter*)>(addr::cSC4RiotList_Insert)(
            riot + field::kRioters, &inserted, At<RioterItem*>(riot, field::kRioters), &fresh);
        Virtual<void>(automaton, 0xCC, int32_t{8}, int32_t{0}, -1.0f, int32_t{1}, int32_t{0}, int32_t{0}, int32_t{0});
        Virtual<void>(automaton, 0x24, int32_t{1});
        void* const layer = Virtual<void*>(City(), 0x120);
        const uint32_t zoom = At<uint8_t>(riot, field::kZoom);
        void* const occupant = Virtual<void*>(automaton, 0x14);
        Virtual<uint8_t>(layer, 0x34, riot ? riot + 0x8 : nullptr, int32_t{0}, int32_t{0}, occupant, zoom,
                         static_cast<uint32_t>(spawned == 0));
        reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, uint32_t)>(addr::cSC4DisasterInstanceRiot_Agitate)(
            riot, lot, static_cast<uint32_t>(x), static_cast<uint32_t>(z));
        if (fresh.lot)
            Virtual<uint32_t>(fresh.lot, 0x8);
    }
    reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceRiot_UpdatePhase)(riot);
    return automaton != nullptr;
}

// A position along one axis as a cell (16 m), the way the game rounds it:
// to the nearest integer below through the float 12582912 trick, then one
// more when the position is exactly that integer plus one.
int32_t CellOf(float metres)
{
    const double cells = static_cast<double>(metres) * 0.0625;
    const float trick = static_cast<float>(cells - 0.5 + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &trick, 4);
    const auto cell = static_cast<int32_t>(bits + 0xB4C00000);
    return static_cast<double>(cell + 1) == cells ? cell + 1 : cell;
}

// The game's 0x0057DFE0.
void __fastcall RiotTick(char* riot, void*)
{
    const uint32_t now = reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::GZStopwatch_ElapsedMilliseconds)(
        riot + field::kStopwatch);
    if (now < At<uint32_t>(riot, field::kNextTick))
        return;
    At<uint32_t>(riot, field::kNextTick) = now + 1;
    void* const police = Virtual<void*>(Global<void*>(addr::SC4_PoliceSimulator), 0x14);
    for (RioterItem* item = At<RioterItem*>(riot, field::kRioters)->next;
         item != At<RioterItem*>(riot, field::kRioters);) {
        RioterItem* const next = item->next;
        Rioter& rioter = item->rioter;
        bool remove = false;
        float position[3];
        Virtual<void>(Virtual<void*>(rioter.automaton, 0x14), 0x20, position);
        int32_t x;
        int32_t z;
        Virtual<void>(City(), 0x17C, position[0], position[2], &x, &z);
        rioter.time += static_cast<int32_t>(static_cast<int64_t>(1.0 - Calm(riot, police, x, z)));
        int32_t level = rioter.time < 0 ? -1
                                        : static_cast<int32_t>(static_cast<uint32_t>(rioter.time) /
                                                               At<uint32_t>(riot, field::kLevelTime));
        if (!Virtual<uint8_t>(Virtual<void*>(rioter.automaton, 0x14), 0x80, int32_t{1})) {
            level = -1;
            remove = true;
        } else if (level == -1) {
            remove = true;
        }
        if (level != rioter.level) {
            rioter.level = level;
            reinterpret_cast<void(__thiscall*)(void*, void*, int32_t)>(addr::cSC4DisasterInstanceRiot_SetAnimation)(
                riot, rioter.automaton, level);
            const uint32_t levels = At<uint32_t>(riot, field::kLevels);
            if (static_cast<uint32_t>(level) >= levels)
                remove = true;
            const int32_t reached = rioter.level;
            if (reached > rioter.topLevel && static_cast<uint32_t>(reached) < levels) {
                rioter.topLevel = reached;
                if (static_cast<uint32_t>(Count(At<RioterItem*>(riot, field::kRioters))) <
                    Global<uint32_t>(addr::cSC4DisasterManagerRiot_Limits + 4)) {
                    int32_t spawn = At<int32_t*>(riot, field::kSpawns)[reached];
                    if (spawn > 0) {
                        float from[3];
                        Virtual<void>(Virtual<void*>(rioter.automaton, 0x14), 0x20, from);
                        int32_t fromX;
                        int32_t fromZ;
                        Virtual<void>(City(), 0x17C, from[0], from[2], &fromX, &fromZ);
                        for (; spawn; --spawn)
                            reinterpret_cast<uint32_t(__thiscall*)(void*, int32_t, int32_t, uint8_t)>(
                                addr::cSC4DisasterInstanceRiot_AddRioter)(riot, fromX, fromZ, 1);
                    }
                }
                GameString news{};
                NewString(news);
                Format(news, *reinterpret_cast<const char* const*>(addr::cSC4DisasterInstanceRiot_NewsFormat),
                       rioter.level);
                void* const ticker = Virtual<void*>(City(), 0xBC);
                void* const occupant = Virtual<void*>(rioter.automaton, 0x14);
                Virtual<void>(ticker, 0x14, occupant, &news, int32_t{0});
                FreeString(news);
            }
        }
        if (remove) {
            Virtual<void>(rioter.automaton, 0xCC, int32_t{1}, int32_t{0}, -1.0f, int32_t{0}, int32_t{0}, int32_t{0},
                          int32_t{0});
            void* const ticker = Virtual<void*>(City(), 0xBC);
            Virtual<void>(ticker, 0x38, Virtual<void*>(rioter.automaton, 0x14));
            void* const layer = Virtual<void*>(City(), 0x120);
            Virtual<uint8_t>(layer, 0x3C, riot + 0x8, Virtual<void*>(rioter.automaton, 0x14));
            Virtual<uint32_t>(rioter.automaton, 0x8);
            item->previous->next = item->next;
            item->next->previous = item->previous;
            if (rioter.lot)
                Virtual<uint32_t>(rioter.lot, 0x8);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(item);
        } else {
            const float start = Global<float>(addr::cSC4DisasterInstanceRiot_Statics + 4);
            float place[3] = {start, start, start};
            Virtual<void>(rioter.automaton, 0x1C, place);
            const int32_t cellX = CellOf(place[0]);
            const int32_t cellZ = CellOf(place[2]);
            void** const lot = Virtual<void**>(Virtual<void*>(City(), 0xA0), 0x38, cellX, cellZ);
            if (lot && *lot && *lot != rioter.lot) {
                reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, uint32_t)>(
                    addr::cSC4DisasterInstanceRiot_Agitate)(riot, *lot, static_cast<uint32_t>(cellX),
                                                            static_cast<uint32_t>(cellZ));
                void* const now2 = *lot;
                void* const old = rioter.lot;
                if (now2 != old) {
                    if (now2)
                        Virtual<uint32_t>(now2, 0x4);
                    rioter.lot = now2;
                    if (old)
                        Virtual<uint32_t>(old, 0x8);
                }
            }
        }
        item = next;
    }
    reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceRiot_UpdatePhase)(riot);
}

void PushBack(char* vector, int32_t value)
{
    int32_t*& end = At<int32_t*>(vector, 0x4);
    if (end != At<int32_t*>(vector, 0x8)) {
        if (end)
            *end = value;
        ++end;
        return;
    }
    uint8_t tag;
    reinterpret_cast<void(__thiscall*)(void*, int32_t*, const int32_t*, uint8_t*, uint32_t, uint32_t)>(
        addr::SC4PtrVector_InsertN)(vector, end, &value, &tag, 1, 1);
}

// The game's 0x0057E3D0.
void __fastcall RiotLoadTuning(char* riot, void*)
{
    At<uint32_t>(riot, field::kLevels) = 6;
    At<uint32_t>(riot, field::kLevelTime) = 30;
    At<uint32_t>(riot, field::kTuning54) = 3;
    char* const spawns = riot + field::kSpawns;
    At<int32_t*>(spawns, 0x4) = At<int32_t*>(spawns, 0x0);
    for (int32_t value : {0, 1, 0, 1, 0, 0})
        PushBack(spawns, value);
    uint32_t key[3] = {0x6534284A, 0x29B6C670, 0x4A38206D};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, uint32_t{0xA52160F5}, &exemplar, int32_t{0},
                          int32_t{0}))
        return;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    using Uint32Fn = uint8_t(__cdecl*)(void*, uint32_t, uint32_t*);
    reinterpret_cast<Uint32Fn>(addr::SC4_GetPropertyUint32)(properties, 0xEA400620, &At<uint32_t>(riot, field::kLevels));
    reinterpret_cast<Uint32Fn>(addr::SC4_GetPropertyUint32)(properties, 0xEA400621, &At<uint32_t>(riot, field::kLevelTime));
    reinterpret_cast<Uint32Fn>(addr::SC4_GetPropertyUint32)(properties, 0xEA400622, &At<uint32_t>(riot, field::kTuning54));
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, void*)>(addr::SC4_GetPropertyUint32Vector)(properties, 0xEA400623,
                                                                                                  spawns);
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyUint8)(
        properties, 0xCA821B8C, &At<uint8_t>(riot, field::kZoom));
    char* const curve = riot + field::kCurve;
    reinterpret_cast<void(__thiscall*)(void*, const uint32_t*, uint32_t)>(addr::SC4Curve_LoadFromExemplar)(curve, key,
                                                                                                         0xEA400624);
    if ((At<char*>(curve, 0x4) - At<char*>(curve, 0x0)) / 20 == 0) {
        struct {
            float* begin;
            float* end;
            float* capacity;
        } points{};
        static const float kDefaults[4][2] = {{0.0f, 0.0f}, {64.0f, 0.0f}, {65.0f, 1.0f}, {255.0f, 3.0f}};
        for (const auto& point : kDefaults) {
            if (points.end != points.capacity) {
                if (points.end) {
                    points.end[0] = point[0];
                    points.end[1] = point[1];
                }
                points.end += 2;
            } else {
                uint8_t tag;
                reinterpret_cast<void(__thiscall*)(void*, float*, const float*, uint8_t*, uint32_t, uint32_t)>(
                    addr::SC4PointVector_InsertN)(&points, points.end, point, &tag, 1, 1);
            }
        }
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Curve_SetPoints)(curve, &points);
        if (points.begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(points.begin);
    }
    Virtual<uint32_t>(exemplar, 0x8);
}

uint8_t __fastcall RiotDoMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == 0x86AD10EE)
        reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceRiot_Tick)(target - 0x10);
    return 1;
}

// The game's 0x0057E770.
uint8_t __fastcall RiotInit(char* riot, void*)
{
    uint8_t& registered = Global<uint8_t>(addr::cSC4DisasterInstanceRiot_Statics);
    if (!registered) {
        registered = 1;
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                      reinterpret_cast<void*>(addr::cSC4DisasterInstanceRiot_ResetStatics), int32_t{0});
    }
    reinterpret_cast<ThisFn>(addr::cSC4DisasterInstanceRiot_LoadTuning)(riot);
    At<uint8_t>(riot, field::kInited) = 1;
    void* const city = City();
    At<int32_t>(riot, field::kWidth) = Virtual<int32_t>(city, 0x174);
    At<int32_t>(riot, field::kDepth) = Virtual<int32_t>(city, 0x178);
    if (char* const manager = Virtual<char*>(Virtual<void*>(city, 0x120), 0x1C, kRiot)) {
        At<char*>(riot, field::kManager) = manager - 0x8;
        Virtual<uint32_t>(manager - 0x8, 0x4);
    }
    Virtual<uint32_t>(At<char*>(riot, field::kManager) + 0x8, 0x20, riot + 0x8); // AddInstanceToList
    if (void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)())
        Virtual<void>(ticks, 0x14, riot + 0x10, int32_t{0xC0});
    char* const stopwatch = riot + field::kStopwatch;
    reinterpret_cast<void(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_ConstructMode)(stopwatch, 5);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Start)(stopwatch);
    for (RioterItem* item = At<RioterItem*>(riot, field::kRioters)->next;
         item != At<RioterItem*>(riot, field::kRioters); item = item->next) {
        if (!item->rioter.lot)
            continue;
        uint32_t cell[2];
        Virtual<void>(Virtual<void*>(item->rioter.lot, 0xC0), 0x2C, cell);
        reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, uint32_t)>(addr::cSC4DisasterInstanceRiot_Agitate)(
            riot, item->rioter.lot, cell[0], cell[1]);
    }
    return 1;
}

// The serializable at +0xC.
uint32_t __fastcall RiotWrite(char* serializable, void*, void* stream)
{
    char* const riot = serializable - 0xC;
    Virtual<void>(stream, 0x1C, uint16_t{2});
    Virtual<void>(stream, 0x24, At<uint32_t>(riot, field::kPhase));
    reinterpret_cast<uint8_t(__cdecl*)(void*, void*)>(addr::SC4Stream_WriteStopwatch)(stream, riot + field::kStopwatch);
    Virtual<void>(stream, 0x24, At<uint32_t>(riot, field::kNextTick));
    RioterItem* const list = At<RioterItem*>(riot, field::kRioters);
    Virtual<void>(stream, 0x24, static_cast<uint32_t>(Count(list)));
    using WriteReferenceFn = uint8_t(__cdecl*)(void*, void*);
    for (RioterItem* item = At<RioterItem*>(riot, field::kRioters)->next; item != list;) {
        RioterItem* const current = item;
        item = item->next;
        reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference)(stream,
                                                                          Virtual<void*>(current->rioter.automaton, 0x14));
        Virtual<void>(stream, 0x20, current->rioter.time);
        Virtual<void>(stream, 0x20, current->rioter.level);
        Virtual<void>(stream, 0x20, current->rioter.topLevel);
        reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference)(stream, current->rioter.lot);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

bool InstallRiot(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterInstanceRiot_QueryInterface, reinterpret_cast<void*>(&RiotQueryInterface)},
        {addr::cSC4DisasterManagerRiot_LoadTuning, reinterpret_cast<void*>(&RiotManagerLoadTuning)},
        {addr::cSC4DisasterInstanceRiot_Agitate, reinterpret_cast<void*>(&Agitate)},
        {addr::cSC4DisasterInstanceRiot_UpdatePhase, reinterpret_cast<void*>(&UpdatePhase)},
        {addr::cSC4DisasterInstanceRiot_Shutdown, reinterpret_cast<void*>(&RiotShutdown)},
        {addr::cSC4DisasterManagerRiot_Init, reinterpret_cast<void*>(&RiotManagerInit)},
        {addr::cSC4DisasterInstanceRiot_FindLotNear, reinterpret_cast<void*>(&FindLotNear)},
        {addr::cSC4DisasterManagerRiot_ClearTracked, reinterpret_cast<void*>(&ClearTracked)},
        {addr::cSC4DisasterManagerRiot_RemoveInstanceFromList, reinterpret_cast<void*>(&RiotRemoveInstanceFromList)},
        {addr::cSC4DisasterInstanceRiot_Write, reinterpret_cast<void*>(&RiotWrite)},
        {addr::cSC4DisasterManagerRiot_Track, reinterpret_cast<void*>(&Track)},
        {addr::cSC4DisasterInstanceRiot_SetAnimation, reinterpret_cast<void*>(&SetAnimation)},
        {addr::cSC4DisasterInstanceRiot_AddRioter, reinterpret_cast<void*>(&AddRioter)},
        {addr::cSC4DisasterInstanceRiot_Tick, reinterpret_cast<void*>(&RiotTick)},
        {addr::cSC4DisasterInstanceRiot_LoadTuning, reinterpret_cast<void*>(&RiotLoadTuning)},
        {addr::cSC4DisasterInstanceRiot_DoMessage, reinterpret_cast<void*>(&RiotDoMessage)},
        {addr::cSC4DisasterInstanceRiot_Init, reinterpret_cast<void*>(&RiotInit)},
        {addr::cSC4DisasterManagerRiot_AttemptDisasterStart, reinterpret_cast<void*>(&RiotAttemptDisasterStart)},
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

const bool g_riotRegistered = registry::Register({
    "simulators.disasters.riot",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterInstanceRiot_Tick,
    "Our riot manager and instance (docs/disasters.md)",
    false,
    InstallRiot,
});

} // namespace
} // namespace opensc4::disasters
