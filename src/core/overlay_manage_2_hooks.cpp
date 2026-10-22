// The detours of our overlay manager's bookkeeping, part 2 (ours in
// overlay_manage_2.cpp; docs/renderer.md, "Bookkeeping"), and its compare
// mode.
//
// Every caller in the exe calls these through the vtable or with plain
// thiscall/cdecl frames and keeps nothing in EAX, ECX or EDX across the call;
// no jump lands inside the first five bytes of any of them.
//
// Compare mode: a call from the game's code to one of the interface's slots
// 3 to 11 or to BuildDecal runs ours on a copy of the manager, then the
// game's on the manager itself, and compares the two: the manager's fields,
// the vectors' sizes and capacities, the records the call can reach (the
// free records it may take, every record past the old end; a whole vector
// when it may grow) and the ones made, every bucket list, the terrain
// redraws (both sides get a stand-in terrain; the game's are then passed on)
// and the result. The copy holds its own references to the textures of the
// records it copies, so what ours assigns and releases there leaves the
// game's counts alone. A vector that may grow is copied whole into a block
// from the game's allocator, which ours may free. Inside a compared call
// each side calls its own: the game's reaches the game's functions, ours
// ours, and part 1's as part 1 is set. The record copies and the vectors'
// growth are reached only from inside these calls; the two range assigns
// are called by the game's Clear (0x0073AC90) with empty ranges and are
// compared by their results.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/overlay_manage.h"
#include "core/overlay_manage_2.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace opensc4::overlay_manage::part2 {
namespace {

constexpr std::size_t kVectors[] = {kOverlays, kHandles, kGroups, kBuckets};
constexpr std::size_t kRecordSizes[] = {kOverlaySize, kHandleSize, kGroupSize};
constexpr std::size_t kTerrainRedraw = 0x110;

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

bool g_compare = false;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("overlays manage2: first %s", name);
}

std::string Hex32(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof text, "%08X", static_cast<unsigned>(value));
    return text;
}

uint32_t Used(char* self, int v)
{
    return static_cast<uint32_t>((At<char*>(self, kVectors[v] + kEnd) - At<char*>(self, kVectors[v])) / kRecordSizes[v]);
}

uint32_t Room(char* self, int v)
{
    return static_cast<uint32_t>((At<char*>(self, kVectors[v] + kCapacity) - At<char*>(self, kVectors[v] + kEnd)) / kRecordSizes[v]);
}

// An overlay record's references, taken or given back.
void Own(char* record)
{
    if (void* const texture = At<void*>(record, kTexture))
        This<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
    if (void* const source = At<void*>(record, kTextureSource))
        Virtual<uint32_t>(source, 0x4);
}

void Disown(char* record)
{
    if (void* const source = At<void*>(record, kTextureSource))
        Virtual<uint32_t>(source, 0x8);
    if (void* const texture = At<void*>(record, kTexture))
        This<uint32_t>(addr::SC4TextureBinding_Release, texture);
}

// ====================================================================================
// Compare mode
// ====================================================================================

// A terrain redraw as the stand-in terrain records it.
struct Redraw {
    uint32_t first, water;
    int32_t rect[4];
    uint32_t last;
    bool operator==(const Redraw& o) const
    {
        return first == o.first && water == o.water && std::memcmp(rect, o.rect, sizeof rect) == 0 && last == o.last;
    }
};

struct StandInTerrain {
    void** vtable;
    std::vector<Redraw>* redraws;
};

void __fastcall StandInRedraw(StandInTerrain* self, void*, uint32_t first, uint32_t water, const int32_t* rect, uint32_t last)
{
    self->redraws->push_back({first, water, {rect[0], rect[1], rect[2], rect[3]}, last});
}

void* g_standInVtable[kTerrainRedraw / 4 + 1] = {};

// What a call can reach: the records it may read or write, the vectors that
// may grow (copied whole), and the cells whose buckets may lose nodes.
struct Reach {
    std::vector<uint32_t> records[3];
    bool whole[3] = {};
    std::vector<std::array<int32_t, 4>> removals;
};

// The free records an add may take from vector v (n at most) are reached;
// the vector is copied whole when the rest would not fit.
void ReachTaken(char* self, int v, uint32_t n, Reach& reach)
{
    char* const vector = self + kVectors[v];
    uint32_t taken = 0;
    for (uint32_t index = At<uint32_t>(vector, kFirstFree); index != kNone && taken < n && index < Used(self, v); ++taken) {
        reach.records[v].push_back(index);
        if (index == At<uint32_t>(vector, kLastFree)) {
            ++taken;
            break;
        }
        index = At<uint32_t>(At<char*>(vector, 0) + index * kRecordSizes[v], 0);
    }
    const uint32_t last = At<uint32_t>(vector, kLastFree);
    if (last < Used(self, v))
        reach.records[v].push_back(last);
    if (taken < n && Room(self, v) < n - taken)
        reach.whole[v] = true;
}

Reach ReachAdd(char* self, uint32_t overlays, uint32_t handles, uint32_t groups)
{
    Reach reach;
    ReachTaken(self, 0, overlays, reach);
    if (handles)
        ReachTaken(self, 1, handles, reach);
    if (groups)
        ReachTaken(self, 2, groups, reach);
    return reach;
}

void ReachOverlay(char* self, uint32_t index, Reach& reach)
{
    if (index >= Used(self, 0))
        return;
    reach.records[0].push_back(index);
    const int32_t* cells = &At<int32_t>(At<char*>(self, kOverlays) + index * kOverlaySize, kCells);
    reach.removals.push_back({cells[0], cells[1], cells[2], cells[3]});
}

// BuildDecal's: the handle (when it is the vector's), its overlay or its
// group and the group's eight; their cells may leave their buckets.
Reach ReachBuild(char* self, const char* handle)
{
    Reach reach;
    const char* const begin = At<char*>(self, kHandles);
    if (handle >= begin && handle < At<char*>(self, kHandles + kEnd))
        reach.records[1].push_back(static_cast<uint32_t>((handle - begin) / kHandleSize));
    const uint32_t target = At<uint32_t>(const_cast<char*>(handle), kTarget);
    if (static_cast<int32_t>(target) >= 0) {
        ReachOverlay(self, target, reach);
        return reach;
    }
    const uint32_t group = target & kIndexMask;
    if (group >= Used(self, 2))
        return reach;
    reach.records[2].push_back(group);
    const uint32_t* ids = &At<uint32_t>(At<char*>(self, kGroups) + group * kGroupSize, kGroupIds);
    for (int i = 0; i < kGroupCount; ++i)
        ReachOverlay(self, ids[i], reach);
    return reach;
}

// The bucket range of a cell rectangle (part 1's).
struct BucketRange {
    int32_t x0, z0, x1, z1;
};

BucketRange Buckets(char* self, const int32_t* cells)
{
    const int shift = At<int32_t>(self, 0xE4) & 31;
    const int32_t round = (1 << shift) - 1;
    return {cells[0] >> shift, cells[1] >> shift, (cells[2] + round) >> shift, (cells[3] + round) >> shift};
}

// A copy of a manager for ours. The records the call can reach are copied
// into buffers the size of the vectors, at the same places, holding their
// own references; a vector that may grow is copied whole into a block from
// the game's allocator (which ours may free and replace). The buckets that
// may lose nodes get copies of their lists; the others share the real ones,
// onto which ours can only push. At the end the copy's references are given
// back and its nodes and blocks freed.
class Shadow {
public:
    Shadow(char* real, const Reach& reach) : real_(real), reach_(reach)
    {
        std::memcpy(object_, real, kSize);
        for (int v = 0; v < 3; ++v) {
            char* const begin = At<char*>(real, kVectors[v]);
            const std::size_t used = At<char*>(real, kVectors[v] + kEnd) - begin;
            const std::size_t capacity = At<char*>(real, kVectors[v] + kCapacity) - begin;
            used_[v] = static_cast<uint32_t>(used / kRecordSizes[v]);
            char* copy = nullptr;
            if (reach.whole[v]) {
                if (capacity)
                    copy = Cdecl<char*>(addr::SC4_stl_allocate, capacity);
                if (used)
                    std::memcpy(copy, begin, used);
                if (v == 0)
                    for (uint32_t i = 0; i < used_[v]; ++i)
                        Own(copy + i * kOverlaySize);
            } else {
                if (Buffer(v).size() < capacity)
                    Buffer(v).resize(capacity);
                copy = begin ? Buffer(v).data() : nullptr;
                for (uint32_t index : reach.records[v]) {
                    if (owned_[v].count(index))
                        continue;
                    owned_[v].insert(index);
                    std::memcpy(copy + index * kRecordSizes[v], begin + index * kRecordSizes[v], kRecordSizes[v]);
                    if (v == 0)
                        Own(copy + index * kOverlaySize);
                }
            }
            At<char*>(object_, kVectors[v]) = copy;
            At<char*>(object_, kVectors[v] + kEnd) = copy + used;
            At<char*>(object_, kVectors[v] + kCapacity) = copy + capacity;
        }
        uint32_t** const begin = At<uint32_t**>(real, kBuckets);
        count_ = At<uint32_t**>(real, kBuckets + kEnd) - begin;
        const std::size_t capacity = At<uint32_t**>(real, kBuckets + kCapacity) - begin;
        std::vector<uint32_t*>& heads = Heads();
        std::vector<uint32_t*>& stops = Stops();
        if (heads.size() < capacity) {
            heads.resize(capacity);
            stops.resize(capacity);
        }
        if (count_) {
            std::memcpy(heads.data(), begin, count_ * sizeof(uint32_t*));
            std::memcpy(stops.data(), begin, count_ * sizeof(uint32_t*));
        }
        for (const auto& cells : reach.removals) {
            const BucketRange r = Buckets(real, cells.data());
            for (int32_t z = r.z0; z < r.z1; ++z)
                for (int32_t x = r.x0; x < r.x1; ++x) {
                    const std::size_t i = static_cast<std::size_t>(At<int32_t>(real, 0xE8) * z + x);
                    if (i >= count_ || (stops[i] == nullptr && heads[i] == nullptr) || copied_.count(i))
                        continue;
                    copied_.insert(i);
                    uint32_t** link = &heads[i];
                    for (const uint32_t* node = begin[i]; node; node = reinterpret_cast<const uint32_t*>(node[0])) {
                        uint32_t* const copy = Cdecl<uint32_t*>(addr::SC4_stl_allocate, std::size_t{8});
                        copy[0] = 0;
                        copy[1] = node[1];
                        *link = copy;
                        link = reinterpret_cast<uint32_t**>(copy);
                    }
                    stops[i] = nullptr;
                }
        }
        uint32_t** const shadowHeads = begin ? heads.data() : nullptr;
        At<uint32_t**>(object_, kBuckets) = shadowHeads;
        At<uint32_t**>(object_, kBuckets + kEnd) = shadowHeads + count_;
        At<uint32_t**>(object_, kBuckets + kCapacity) = shadowHeads + capacity;
    }

    ~Shadow()
    {
        char* const overlays = At<char*>(object_, kOverlays);
        const uint32_t used = Used(object_, 0);
        if (reach_.whole[0]) {
            for (uint32_t i = 0; i < used; ++i)
                Disown(overlays + i * kOverlaySize);
        } else {
            for (uint32_t index : owned_[0])
                if (index < used)
                    Disown(overlays + index * kOverlaySize);
            for (uint32_t i = used_[0]; i < used; ++i)
                if (!owned_[0].count(i))
                    Disown(overlays + i * kOverlaySize);
        }
        for (int v = 0; v < 3; ++v)
            if (reach_.whole[v] && At<char*>(object_, kVectors[v]))
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(At<char*>(object_, kVectors[v])));
        uint32_t** const heads = At<uint32_t**>(object_, kBuckets);
        const std::vector<uint32_t*>& stops = Stops();
        for (std::size_t i = 0; i < count_; ++i)
            for (uint32_t* node = heads[i]; node && node != stops[i];) {
                uint32_t* const next = reinterpret_cast<uint32_t*>(node[0]);
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
                node = next;
            }
    }

    Shadow(const Shadow&) = delete;
    Shadow& operator=(const Shadow&) = delete;

    char* self() { return object_; }

    // A pointer into the real manager or its vectors, moved to the copy's.
    template <typename T>
    T* Map(T* pointer) const
    {
        const char* const p = reinterpret_cast<const char*>(pointer);
        if (p >= real_ && p < real_ + kSize)
            return reinterpret_cast<T*>(const_cast<char*>(object_) + (p - real_));
        for (int v = 0; v < 3; ++v) {
            const char* const begin = At<char*>(real_, kVectors[v]);
            const char* const end = At<char*>(real_, kVectors[v] + kCapacity);
            if (begin && p >= begin && p < end)
                return reinterpret_cast<T*>(At<char*>(const_cast<char*>(object_), kVectors[v]) + (p - begin));
        }
        return pointer;
    }

    // The first difference between the real manager and the copy, or empty:
    // the manager's fields (the vectors by size), the reached records and
    // every one past the old end (all of a vector copied whole; an overlay's
    // byte at +9 left out, which the game's copies never set), every bucket
    // (a shared list both left alone has the same head).
    std::string Diff() const
    {
        char* const ours = const_cast<char*>(object_);
        for (std::size_t at = 0; at < kSize; at += 4) {
            bool pointer = at == kTerrain;
            for (std::size_t v : kVectors)
                pointer = pointer || (at >= v && at < v + 12);
            if (pointer)
                continue;
            if (At<uint32_t>(real_, at) != At<uint32_t>(ours, at))
                return Field(at, At<uint32_t>(real_, at), At<uint32_t>(ours, at));
        }
        for (int v = 0; v < 4; ++v)
            for (std::size_t end : {kEnd, kCapacity}) {
                const std::size_t a = At<char*>(real_, kVectors[v] + end) - At<char*>(real_, kVectors[v]);
                const std::size_t b = At<char*>(ours, kVectors[v] + end) - At<char*>(ours, kVectors[v]);
                if (a != b)
                    return Field(kVectors[v] + end, static_cast<uint32_t>(a), static_cast<uint32_t>(b));
            }
        static const char* const kNames[] = {"overlay", "handle", "group"};
        for (int v = 0; v < 3; ++v) {
            std::set<uint32_t> indices(reach_.records[v].begin(), reach_.records[v].end());
            const uint32_t used = Used(ours, v);
            for (uint32_t i = reach_.whole[v] ? 0 : used_[v]; i < used; ++i)
                indices.insert(i);
            for (uint32_t index : indices) {
                if (index >= used)
                    continue;
                const char* const a = At<char*>(real_, kVectors[v]) + index * kRecordSizes[v];
                const char* const b = At<char*>(ours, kVectors[v]) + index * kRecordSizes[v];
                for (std::size_t at = 0; at < kRecordSizes[v]; at += 4) {
                    uint32_t x, y;
                    std::memcpy(&x, a + at, 4);
                    std::memcpy(&y, b + at, 4);
                    if (v == 0 && at == kDrawMode) {
                        x &= 0xFFFF00FF;
                        y &= 0xFFFF00FF;
                    }
                    if (x != y) {
                        char where[48];
                        std::snprintf(where, sizeof where, "%s %u +0x%X: ", kNames[v], static_cast<unsigned>(index),
                                      static_cast<unsigned>(at));
                        return std::string(where) + Hex32(x) + " / " + Hex32(y);
                    }
                }
            }
        }
        uint32_t** const a = At<uint32_t**>(real_, kBuckets);
        uint32_t** const b = At<uint32_t**>(ours, kBuckets);
        for (std::size_t i = 0; i < count_; ++i)
            for (const uint32_t *x = a[i], *y = b[i]; x != y;) {
                if (!x || !y || x[1] != y[1])
                    return "bucket " + std::to_string(i) + ": " + (x ? Hex32(x[1]) : std::string("end")) + " / "
                           + (y ? Hex32(y[1]) : std::string("end"));
                x = reinterpret_cast<const uint32_t*>(x[0]);
                y = reinterpret_cast<const uint32_t*>(y[0]);
            }
        return {};
    }

private:
    static std::string Field(std::size_t at, uint32_t game, uint32_t ours)
    {
        char text[64];
        std::snprintf(text, sizeof text, "manager +0x%X: %08X / %08X", static_cast<unsigned>(at), static_cast<unsigned>(game),
                      static_cast<unsigned>(ours));
        return text;
    }

    // Kept from call to call (compared calls do not nest).
    static std::vector<char>& Buffer(int v)
    {
        static std::vector<char> buffers[3];
        return buffers[v];
    }
    static std::vector<uint32_t*>& Heads()
    {
        static std::vector<uint32_t*> heads;
        return heads;
    }
    static std::vector<uint32_t*>& Stops()
    {
        static std::vector<uint32_t*> stops;
        return stops;
    }

    alignas(8) char object_[kSize + 4];
    char* real_;
    const Reach& reach_;
    uint32_t used_[3] = {};
    std::set<uint32_t> owned_[3];
    std::size_t count_ = 0;
    std::set<std::size_t> copied_;
};

uint64_t g_stateCompared = 0;

// A call that changes the manager, compared: ours on a copy, the game's on
// the manager. `ours` is given the copy; `game` runs the original. Both with
// stand-in terrains; the game's redraws are passed on to the real terrain
// afterwards.
template <typename Ours, typename Game>
auto CompareState(const char* id, char* self, const Reach& reach, Ours&& ours, Game&& game)
{
    std::vector<Redraw> ourRedraws, gameRedraws;
    StandInTerrain ourTerrain{g_standInVtable, &ourRedraws}, gameTerrain{g_standInVtable, &gameRedraws};
    void* const terrain = At<void*>(self, kTerrain);
    Shadow shadow(self, reach);
    At<void*>(shadow.self(), kTerrain) = &ourTerrain;
    g_side = Side::Ours;
    const uint32_t mine = ours(shadow);
    At<void*>(self, kTerrain) = &gameTerrain;
    g_side = Side::Game;
    const uint32_t result = game();
    g_side = Side::None;
    At<void*>(self, kTerrain) = terrain;
    for (Redraw& r : gameRedraws)
        Virtual<void>(terrain, kTerrainRedraw, r.first, r.water, r.rect, r.last);
    std::string diff = shadow.Diff();
    if (diff.empty() && ourRedraws != gameRedraws) {
        std::size_t at = 0;
        while (at < ourRedraws.size() && at < gameRedraws.size() && ourRedraws[at] == gameRedraws[at])
            ++at;
        diff = "redraw " + std::to_string(at) + " of " + std::to_string(gameRedraws.size()) + " / " + std::to_string(ourRedraws.size());
    }
    if (diff.empty() && result != mine)
        diff = "result " + Hex32(result) + " / " + Hex32(mine);
    compare::Record(id, diff.empty(), diff, diff);
    ++g_stateCompared;
    if ((g_stateCompared & (g_stateCompared - 1)) == 0)
        log::Write("overlays manage2: %llu calls compared", static_cast<unsigned long long>(g_stateCompared));
    return result;
}

// Ours, the game's, or in compare mode at the top `compared`.
template <typename Ours, typename Game, typename Compared>
auto Route(Ours&& ours, Game&& game, Compared&& compared)
{
    if (!g_compare || g_side == Side::Ours)
        return ours();
    if (g_side == Side::Game)
        return game();
    return compared();
}

// For the functions only the others reach: at the top in compare mode the
// game's, once noted.
template <typename Ours, typename Game>
auto RouteInner(const char* name, Ours&& ours, Game&& game)
{
    if (!g_compare || g_side == Side::Ours)
        return ours();
    if (g_side == Side::None) {
        static std::set<std::string> noted;
        if (noted.insert(name).second)
            log::Write("overlays manage2: %s called outside a compared call, not compared", name);
    }
    return game();
}

// ====================================================================================
// Detours
// ====================================================================================

using AddFn = uint32_t(__fastcall*)(char*, void*, const void*, const float*, float, float);
using AddMirrorFn = uint32_t(__fastcall*)(char*, void*, const void*, const float*, float, float, uint32_t);
using AddFromFn = uint32_t(__fastcall*)(char*, void*, void*, void*, const float*, float, float);
using HeightMapFn = uint32_t(__fastcall*)(char*, void*, uint32_t);
using ShadowFn = uint32_t(__fastcall*)(char*, void*, void*, int32_t, const float*, const float*, const float*, const void*);
using AddOverlayFn = uint32_t(__fastcall*)(char*, void*, void*, const int32_t*, const float*, uint32_t);
using BuildFn = void(__fastcall*)(char*, void*, char*, uint32_t);
using RingFn = void(__fastcall*)(char*, void*, const uint32_t*, const float*, uint32_t);
using ShadowCellsFn = void(__fastcall*)(char*, void*, const float*, uint32_t, const void*, const float*, int32_t*);
using RecordFn = char*(__fastcall*)(char*, void*, const char*);
using ResetFn = char*(__fastcall*)(char*);
using RangeFn = char*(__cdecl*)(const char*, const char*, char*);
using FillFn = char*(__cdecl*)(char*, uint32_t, const char*);
using CopyHandleFn = char*(__cdecl*)(char*, const char*);
using GroupsFn = uint32_t*(__cdecl*)(const uint32_t*, const uint32_t*, uint32_t*);
using GrowFn = void(__fastcall*)(char*, void*, char*, const char*, const void*, uint32_t, uint32_t);
using NewFn = uint32_t(__fastcall*)(char*, void*, const char*);

AddFn g_addSource, g_addTexture, g_addKey, g_addIid, g_addRing;
AddMirrorFn g_shadowDecalKey, g_shadowDecalIid;
AddFromFn g_addFrom;
HeightMapFn g_heightMap;
ShadowFn g_shadow;
AddOverlayFn g_addOverlay;
BuildFn g_build;
RingFn g_ring;
ShadowCellsFn g_shadowCells;
RecordFn g_assign, g_copy;
ResetFn g_reset;
RangeFn g_assignOverlays, g_assignHandles, g_copyOverlays, g_copyHandles;
FillFn g_fillOverlays, g_fillHandles;
CopyHandleFn g_copyHandle;
GroupsFn g_copyGroups;
GrowFn g_growOverlays, g_growHandles, g_growGroups;
NewFn g_newOverlay, g_newHandle, g_newGroup;

#define REACHED(name)                                                                                                  \
    static bool reached = false;                                                                                       \
    FirstCall(name, reached)

// The slots: compared at the top with what they may take.
uint32_t __fastcall DAddSource(char* self, void*, const void* source, const float* position, float size, float turn)
{
    REACHED("AddDecal (source)");
    void* const s = const_cast<void*>(source);
    return Route([&] { return AddDecalSource(self, nullptr, s, position, size, turn); },
                 [&] { return g_addSource(self, nullptr, source, position, size, turn); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addsource", self, ReachAdd(self, 1, 1, 0),
                                         [&](Shadow& sh) { return AddDecalSource(sh.self(), nullptr, s, position, size, turn); },
                                         [&] { return g_addSource(self, nullptr, source, position, size, turn); });
                 });
}

uint32_t __fastcall DAddTexture(char* self, void*, const void* texture, const float* position, float size, float turn)
{
    REACHED("AddDecal (texture)");
    void* const t = const_cast<void*>(texture);
    return Route([&] { return AddDecalTexture(self, nullptr, t, position, size, turn); },
                 [&] { return g_addTexture(self, nullptr, texture, position, size, turn); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addtexture", self, ReachAdd(self, 1, 1, 0),
                                         [&](Shadow& sh) { return AddDecalTexture(sh.self(), nullptr, t, position, size, turn); },
                                         [&] { return g_addTexture(self, nullptr, texture, position, size, turn); });
                 });
}

uint32_t __fastcall DAddKey(char* self, void*, const void* key, const float* position, float size, float turn)
{
    REACHED("AddDecal (key)");
    const auto* k = static_cast<const uint32_t*>(key);
    return Route([&] { return AddDecalKey(self, nullptr, k, position, size, turn); },
                 [&] { return g_addKey(self, nullptr, key, position, size, turn); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addkey", self, ReachAdd(self, 1, 1, 0),
                                         [&](Shadow& sh) { return AddDecalKey(sh.self(), nullptr, k, position, size, turn); },
                                         [&] { return g_addKey(self, nullptr, key, position, size, turn); });
                 });
}

uint32_t __fastcall DAddIid(char* self, void*, const void* iid, const float* position, float size, float turn)
{
    REACHED("AddDecal (ID)");
    const uint32_t i = reinterpret_cast<uint32_t>(iid);
    return Route([&] { return AddDecalIid(self, nullptr, i, position, size, turn); },
                 [&] { return g_addIid(self, nullptr, iid, position, size, turn); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addiid", self, ReachAdd(self, 1, 1, 0),
                                         [&](Shadow& sh) { return AddDecalIid(sh.self(), nullptr, i, position, size, turn); },
                                         [&] { return g_addIid(self, nullptr, iid, position, size, turn); });
                 });
}

uint32_t __fastcall DAddRing(char* self, void*, const void* iid, const float* position, float size, float turn)
{
    REACHED("AddRingDecal");
    const uint32_t i = reinterpret_cast<uint32_t>(iid);
    return Route([&] { return AddRingDecal(self, nullptr, i, position, size, turn); },
                 [&] { return g_addRing(self, nullptr, iid, position, size, turn); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addring", self, ReachAdd(self, kGroupCount, 1, 1),
                                         [&](Shadow& sh) { return AddRingDecal(sh.self(), nullptr, i, position, size, turn); },
                                         [&] { return g_addRing(self, nullptr, iid, position, size, turn); });
                 });
}

uint32_t __fastcall DHeightMap(char* self, void*, uint32_t iid)
{
    REACHED("AddHeightMap");
    return Route([&] { return AddHeightMap(self, nullptr, iid); }, [&] { return g_heightMap(self, nullptr, iid); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addheightmap", self, ReachAdd(self, 1, 0, 0),
                                         [&](Shadow& sh) { return AddHeightMap(sh.self(), nullptr, iid); },
                                         [&] { return g_heightMap(self, nullptr, iid); });
                 });
}

uint32_t __fastcall DShadowDecalKey(char* self, void*, const void* key, const float* position, float size, float turn, uint32_t mirror)
{
    REACHED("AddShadowDecal (key)");
    const auto* k = static_cast<const uint32_t*>(key);
    return Route([&] { return AddShadowDecalKey(self, nullptr, k, position, size, turn, mirror); },
                 [&] { return g_shadowDecalKey(self, nullptr, key, position, size, turn, mirror); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addshadowdecalkey", self, ReachAdd(self, 1, 1, 0),
                                         [&](Shadow& sh) { return AddShadowDecalKey(sh.self(), nullptr, k, position, size, turn, mirror); },
                                         [&] { return g_shadowDecalKey(self, nullptr, key, position, size, turn, mirror); });
                 });
}

uint32_t __fastcall DShadowDecalIid(char* self, void*, const void* iid, const float* position, float size, float turn, uint32_t mirror)
{
    REACHED("AddShadowDecal (ID)");
    const uint32_t i = reinterpret_cast<uint32_t>(iid);
    return Route([&] { return AddShadowDecalIid(self, nullptr, i, position, size, turn, mirror); },
                 [&] { return g_shadowDecalIid(self, nullptr, iid, position, size, turn, mirror); },
                 [&] {
                     return CompareState("renderer.overlays.manage2.addshadowdecaliid", self, ReachAdd(self, 1, 1, 0),
                                         [&](Shadow& sh) { return AddShadowDecalIid(sh.self(), nullptr, i, position, size, turn, mirror); },
                                         [&] { return g_shadowDecalIid(self, nullptr, iid, position, size, turn, mirror); });
                 });
}

uint32_t __fastcall DShadow(char* self, void*, void* texture, int32_t count, const float* points3, const float* points2,
                            const float* box, const void* transform)
{
    REACHED("AddShadow");
    return Route([&] { return AddShadow(self, nullptr, texture, count, points3, points2, box, transform); },
                 [&] { return g_shadow(self, nullptr, texture, count, points3, points2, box, transform); },
                 [&] {
                     return CompareState(
                         "renderer.overlays.manage2.addshadow", self, ReachAdd(self, 1, 0, 0),
                         [&](Shadow& sh) { return AddShadow(sh.self(), nullptr, texture, count, points3, points2, box, transform); },
                         [&] { return g_shadow(self, nullptr, texture, count, points3, points2, box, transform); });
                 });
}

void __fastcall DBuild(char* self, void*, char* handle, uint32_t buckets)
{
    REACHED("BuildDecal");
    Route([&] { BuildDecal(self, nullptr, handle, buckets); }, [&] { g_build(self, nullptr, handle, buckets); },
          [&] {
              CompareState(
                  "renderer.overlays.manage2.build", self, ReachBuild(self, handle),
                  [&](Shadow& sh) {
                      BuildDecal(sh.self(), nullptr, sh.Map(handle), buckets);
                      return 0u;
                  },
                  [&] {
                      g_build(self, nullptr, handle, buckets);
                      return 0u;
                  });
          });
}

// Reached only from the slots above (and part 1's MoveDecal and
// UpdateDecalInfo for BuildDecal).
uint32_t __fastcall DAddFrom(char* self, void*, void* source, void* texture, const float* position, float size, float turn)
{
    REACHED("AddDecalFrom");
    return RouteInner("AddDecalFrom", [&] { return AddDecalFrom(self, nullptr, source, texture, position, size, turn); },
                      [&] { return g_addFrom(self, nullptr, source, texture, position, size, turn); });
}

uint32_t __fastcall DAddOverlay(char* self, void*, void* texture, const int32_t* cells, const float* matrix, uint32_t flags)
{
    REACHED("AddOverlay");
    return RouteInner("AddOverlay", [&] { return AddOverlay(self, nullptr, texture, cells, matrix, flags); },
                      [&] { return g_addOverlay(self, nullptr, texture, cells, matrix, flags); });
}

void __fastcall DRing(char* self, void*, const uint32_t* group, const float* rect, uint32_t buckets)
{
    REACHED("BuildRing");
    RouteInner("BuildRing", [&] { BuildRing(self, nullptr, group, rect, buckets); }, [&] { g_ring(self, nullptr, group, rect, buckets); });
}

void __fastcall DShadowCells(char* self, void*, const float* light, uint32_t count, const void* points, const float* box, int32_t* out)
{
    REACHED("ShadowCells");
    RouteInner("ShadowCells", [&] { ShadowCells(self, nullptr, light, count, points, box, out); },
               [&] { g_shadowCells(self, nullptr, light, count, points, box, out); });
}

char* __fastcall DAssign(char* to, void*, const char* from)
{
    REACHED("AssignOverlay");
    return RouteInner("AssignOverlay", [&] { return AssignOverlay(to, nullptr, from); }, [&] { return g_assign(to, nullptr, from); });
}

char* __fastcall DCopy(char* to, void*, const char* from)
{
    REACHED("CopyOverlay");
    return RouteInner("CopyOverlay", [&] { return CopyOverlay(to, nullptr, from); }, [&] { return g_copy(to, nullptr, from); });
}

char* __fastcall DReset(char* record)
{
    REACHED("ResetOverlay");
    return RouteInner("ResetOverlay", [&] { return ResetOverlay(record); }, [&] { return g_reset(record); });
}

// The range helpers called at the top in compare mode (Clear's empty
// assigns, the autotest's direct calls): ours into a scratch block, made as a
// copy of the destination when ours assigns to it, the game's into the
// destination; the ends and the bytes compared (an overlay's byte at +9 left
// out), ours' overlay copies destroyed after.
template <typename Ours, typename Game>
char* CompareRange(const char* id, char* to, int32_t count, int v, bool assign, Ours&& ours, Game&& game)
{
    const std::size_t size = kRecordSizes[v];
    const std::size_t bytes = count > 0 ? static_cast<std::size_t>(count) * size : 0;
    std::vector<char> scratch(bytes + 4);
    char* const mine = to ? scratch.data() : nullptr;
    g_side = Side::Game;
    if (mine && assign) {
        if (v == 0)
            g_copyOverlays(to, to + bytes, mine);
        else
            std::memcpy(mine, to, bytes);
    }
    g_side = Side::Ours;
    char* const ourEnd = ours(mine);
    g_side = Side::Game;
    char* const end = game();
    std::string diff;
    if (end - to != ourEnd - mine)
        diff = "end " + Hex32(static_cast<uint32_t>(end - to)) + " / " + Hex32(static_cast<uint32_t>(ourEnd - mine));
    for (std::size_t at = 0; diff.empty() && mine && at < bytes; at += 4) {
        uint32_t x, y;
        std::memcpy(&x, to + at, 4);
        std::memcpy(&y, mine + at, 4);
        if (v == 0 && at % kOverlaySize == kDrawMode) {
            x &= 0xFFFF00FF;
            y &= 0xFFFF00FF;
        }
        if (x != y)
            diff = "+0x" + Hex32(static_cast<uint32_t>(at)) + ": " + Hex32(x) + " / " + Hex32(y);
    }
    if (mine && v == 0 && bytes) {
        char tag = 0;
        Cdecl<void>(addr::cSC4OverlayManager_DestroyOverlays, mine, mine + bytes, static_cast<void*>(&tag));
    }
    g_side = Side::None;
    compare::Record(id, diff.empty(), diff, diff);
    return end;
}

int32_t Count(const void* first, const void* last, int v)
{
    return static_cast<int32_t>(static_cast<const char*>(last) - static_cast<const char*>(first)) / static_cast<int32_t>(kRecordSizes[v]);
}

char* __cdecl DCopyOverlays(const char* first, const char* last, char* to)
{
    REACHED("CopyOverlays");
    return Route([&] { return CopyOverlays(first, last, to); }, [&] { return g_copyOverlays(first, last, to); },
                 [&] {
                     return CompareRange("renderer.overlays.manage2.copyoverlays", to, Count(first, last, 0), 0, false,
                                         [&](char* t) { return CopyOverlays(first, last, t); },
                                         [&] { return g_copyOverlays(first, last, to); });
                 });
}

char* __cdecl DCopyHandles(const char* first, const char* last, char* to)
{
    REACHED("CopyHandles");
    return Route([&] { return CopyHandles(first, last, to); }, [&] { return g_copyHandles(first, last, to); },
                 [&] {
                     return CompareRange("renderer.overlays.manage2.copyhandles", to, Count(first, last, 1), 1, false,
                                         [&](char* t) { return CopyHandles(first, last, t); },
                                         [&] { return g_copyHandles(first, last, to); });
                 });
}

char* __cdecl DFillOverlays(char* to, uint32_t count, const char* from)
{
    REACHED("FillOverlays");
    return Route([&] { return FillOverlays(to, count, from); }, [&] { return g_fillOverlays(to, count, from); },
                 [&] {
                     return CompareRange("renderer.overlays.manage2.filloverlays", to, static_cast<int32_t>(count), 0, false,
                                         [&](char* t) { return FillOverlays(t, count, from); },
                                         [&] { return g_fillOverlays(to, count, from); });
                 });
}

char* __cdecl DFillHandles(char* to, uint32_t count, const char* from)
{
    REACHED("FillHandles");
    return Route([&] { return FillHandles(to, count, from); }, [&] { return g_fillHandles(to, count, from); },
                 [&] {
                     return CompareRange("renderer.overlays.manage2.fillhandles", to, static_cast<int32_t>(count), 1, false,
                                         [&](char* t) { return FillHandles(t, count, from); },
                                         [&] { return g_fillHandles(to, count, from); });
                 });
}

char* __cdecl DCopyHandle(char* to, const char* from)
{
    REACHED("CopyHandle");
    return RouteInner("CopyHandle", [&] { return CopyHandle(to, from); }, [&] { return g_copyHandle(to, from); });
}

uint32_t* __cdecl DCopyGroups(const uint32_t* first, const uint32_t* last, uint32_t* to)
{
    REACHED("CopyGroups");
    char* const t = reinterpret_cast<char*>(to);
    return Route([&] { return CopyGroups(first, last, to); }, [&] { return g_copyGroups(first, last, to); },
                 [&] {
                     return reinterpret_cast<uint32_t*>(CompareRange(
                         "renderer.overlays.manage2.copygroups", t, Count(first, last, 2), 2, false,
                         [&](char* mine) {
                             return reinterpret_cast<char*>(CopyGroups(first, last, reinterpret_cast<uint32_t*>(mine)));
                         },
                         [&] { return reinterpret_cast<char*>(g_copyGroups(first, last, to)); }));
                 });
}

void __fastcall DGrowOverlays(char* vector, void*, char* at, const char* from, const void* tag, uint32_t count, uint32_t atEnd)
{
    REACHED("GrowOverlays");
    RouteInner("GrowOverlays", [&] { GrowOverlays(vector, nullptr, at, from, tag, count, atEnd); },
               [&] { g_growOverlays(vector, nullptr, at, from, tag, count, atEnd); });
}

void __fastcall DGrowHandles(char* vector, void*, char* at, const char* from, const void* tag, uint32_t count, uint32_t atEnd)
{
    REACHED("GrowHandles");
    RouteInner("GrowHandles", [&] { GrowHandles(vector, nullptr, at, from, tag, count, atEnd); },
               [&] { g_growHandles(vector, nullptr, at, from, tag, count, atEnd); });
}

void __fastcall DGrowGroups(char* vector, void*, char* at, const char* from, const void* tag, uint32_t count, uint32_t atEnd)
{
    REACHED("GrowGroups");
    RouteInner("GrowGroups", [&] { GrowGroups(vector, nullptr, at, from, tag, count, atEnd); },
               [&] { g_growGroups(vector, nullptr, at, from, tag, count, atEnd); });
}

uint32_t __fastcall DNewOverlay(char* vector, void*, const char* from)
{
    REACHED("NewOverlay");
    return RouteInner("NewOverlay", [&] { return NewOverlay(vector, nullptr, from); }, [&] { return g_newOverlay(vector, nullptr, from); });
}

uint32_t __fastcall DNewHandle(char* vector, void*, const char* from)
{
    REACHED("NewHandle");
    return RouteInner("NewHandle", [&] { return NewHandle(vector, nullptr, from); }, [&] { return g_newHandle(vector, nullptr, from); });
}

uint32_t __fastcall DNewGroup(char* vector, void*, const char* from)
{
    REACHED("NewGroup");
    return RouteInner("NewGroup", [&] { return NewGroup(vector, nullptr, from); }, [&] { return g_newGroup(vector, nullptr, from); });
}

char* __cdecl DAssignOverlays(const char* first, const char* last, char* to)
{
    REACHED("AssignOverlays");
    return Route([&] { return AssignOverlays(first, last, to); }, [&] { return g_assignOverlays(first, last, to); },
                 [&] {
                     return CompareRange("renderer.overlays.manage2.assignoverlays", to, Count(first, last, 0), 0, true,
                                         [&](char* t) { return AssignOverlays(first, last, t); },
                                         [&] { return g_assignOverlays(first, last, to); });
                 });
}

char* __cdecl DAssignHandles(const char* first, const char* last, char* to)
{
    REACHED("AssignHandles");
    return Route([&] { return AssignHandles(first, last, to); }, [&] { return g_assignHandles(first, last, to); },
                 [&] {
                     return CompareRange("renderer.overlays.manage2.assignhandles", to, Count(first, last, 1), 1, true,
                                         [&](char* t) { return AssignHandles(first, last, t); },
                                         [&] { return g_assignHandles(first, last, to); });
                 });
}

#undef REACHED

// ====================================================================================
// Installation
// ====================================================================================

struct Hooked {
    std::uintptr_t address;
    void* ours;
    void** original;
};

template <typename Fn>
Hooked H(std::uintptr_t address, Fn ours, Fn* original)
{
    return {address, reinterpret_cast<void*>(ours), reinterpret_cast<void**>(original)};
}

bool InstallManage2(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    const Hooked hooks[] = {
        H<AddFn>(addr::cSC4OverlayManager_AddDecalSource, &DAddSource, &g_addSource),
        H<AddFn>(addr::cSC4OverlayManager_AddDecalTexture, &DAddTexture, &g_addTexture),
        H<AddFn>(addr::cSC4OverlayManager_AddDecalKey, &DAddKey, &g_addKey),
        H<AddFn>(addr::cSC4OverlayManager_AddDecalIid, &DAddIid, &g_addIid),
        H<AddFn>(addr::cSC4OverlayManager_AddRingDecal, &DAddRing, &g_addRing),
        H<HeightMapFn>(addr::cSC4OverlayManager_AddHeightMap, &DHeightMap, &g_heightMap),
        H<AddMirrorFn>(addr::cSC4OverlayManager_AddShadowDecalKey, &DShadowDecalKey, &g_shadowDecalKey),
        H<AddMirrorFn>(addr::cSC4OverlayManager_AddShadowDecalIid, &DShadowDecalIid, &g_shadowDecalIid),
        H<ShadowFn>(addr::cSC4OverlayManager_AddShadow, &DShadow, &g_shadow),
        H<BuildFn>(addr::cSC4OverlayManager_BuildDecal, &DBuild, &g_build),
        H<AddFromFn>(addr::cSC4OverlayManager_AddDecalFrom, &DAddFrom, &g_addFrom),
        H<AddOverlayFn>(addr::cSC4OverlayManager_AddOverlay, &DAddOverlay, &g_addOverlay),
        H<RingFn>(addr::cSC4OverlayManager_BuildRing, &DRing, &g_ring),
        H<ShadowCellsFn>(addr::cSC4OverlayManager_ShadowCells, &DShadowCells, &g_shadowCells),
        H<RecordFn>(addr::cSC4OverlayManager_AssignOverlay, &DAssign, &g_assign),
        H<RecordFn>(addr::cSC4OverlayManager_CopyOverlay, &DCopy, &g_copy),
        H<ResetFn>(addr::cSC4OverlayManager_ResetOverlay, &DReset, &g_reset),
        H<RangeFn>(addr::cSC4OverlayManager_AssignOverlays, &DAssignOverlays, &g_assignOverlays),
        H<RangeFn>(addr::cSC4OverlayManager_AssignHandles, &DAssignHandles, &g_assignHandles),
        H<RangeFn>(addr::cSC4OverlayManager_CopyOverlays, &DCopyOverlays, &g_copyOverlays),
        H<RangeFn>(addr::cSC4OverlayManager_CopyHandles, &DCopyHandles, &g_copyHandles),
        H<FillFn>(addr::cSC4OverlayManager_FillOverlays, &DFillOverlays, &g_fillOverlays),
        H<FillFn>(addr::cSC4OverlayManager_FillHandles, &DFillHandles, &g_fillHandles),
        H<CopyHandleFn>(addr::cSC4OverlayManager_CopyHandle, &DCopyHandle, &g_copyHandle),
        H<GroupsFn>(addr::cSC4OverlayManager_CopyGroups, &DCopyGroups, &g_copyGroups),
        H<GrowFn>(addr::cSC4OverlayManager_GrowOverlays, &DGrowOverlays, &g_growOverlays),
        H<GrowFn>(addr::cSC4OverlayManager_GrowHandles, &DGrowHandles, &g_growHandles),
        H<GrowFn>(addr::cSC4OverlayManager_GrowGroups, &DGrowGroups, &g_growGroups),
        H<NewFn>(addr::cSC4OverlayManager_NewOverlay, &DNewOverlay, &g_newOverlay),
        H<NewFn>(addr::cSC4OverlayManager_NewHandle, &DNewHandle, &g_newHandle),
        H<NewFn>(addr::cSC4OverlayManager_NewGroup, &DNewGroup, &g_newGroup),
    };
    for (const Hooked& h : hooks)
        if (!addr_check::Find(h.address)) {
            log::Write("overlays manage2: no address entry for 0x%08X", static_cast<unsigned>(h.address));
            return false;
        }
    g_standInVtable[kTerrainRedraw / 4] = reinterpret_cast<void*>(&StandInRedraw);
    g_compare = mode == registry::Mode::Compare;
    for (const Hooked& h : hooks)
        if (!hook::Install(*addr_check::Find(h.address), h.ours, h.original))
            return false;
    log::Write("overlays manage2: %u detours%s", static_cast<unsigned>(std::size(hooks)), g_compare ? ", compared with the game's" : "");
    return true;
}

const bool g_registered = registry::Register({
    "renderer.overlays.manage2",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_AddDecalTexture,
    "The overlay manager's bookkeeping, part 2: the adding (the interface's slots 3 to 11, the new records, the vectors' "
    "growth and the record copies, and the decal builders 0x0073A1B0 and 0x007394B0; 31 detours; compare mode runs ours "
    "on a copy of the manager and compares it with the game's; src/core/overlay_manage_2.cpp)",
    true,
    InstallManage2,
});

} // namespace
} // namespace opensc4::overlay_manage::part2
