// Our overlay manager's bookkeeping, part 1 (cSC4OverlayManager, build 641;
// docs/renderer.md, "The overlays", "Bookkeeping"): the interface's getters
// and setters, removing, moving and updating an overlay, the grid of buckets
// the overlays are kept in, and a few small functions of the class.
//
// The manager (0xFC bytes, constructor 0x0073BC20) keeps three vectors of
// records: the overlays (+0x98, 0xB4 bytes each), the decal handles (+0xAC,
// 0x30 bytes: a decal's description at +4 and its target at +0x2C) and the
// groups (+0xC0, 0x24 bytes: eight IDs at +4). Each has a free list through
// the records' first field (-1 while a record is in use; the first and the
// last free record after the vector: +0xA4/+0xA8, +0xB8/+0xBC, +0xCC/+0xD0).
// An ID is an overlay's index, or with the top bit set a handle's; a
// handle's target is an overlay's index, or with the top bit set a group's.
// The buckets (+0xF0) are a grid (+0xE8 a row, cells shifted right by
// +0xE4) of lists of overlay indices, nodes {next, overlay} from the game's
// STL allocator. A manager that draws the water's rows (+0x15) has none.
//
// The adding is part 2 (overlay_manage_2.cpp): MoveDecal and UpdateDecalInfo
// call its BuildDecal (0x0073A1B0) by address, which reaches ours when
// renderer.overlays.manage2 is on and the game's when it is off. What stays
// the game's: the constructor, destructor and set-up, the draws
// (terrain_draw.cpp) and the adjustor thunks of the other interfaces
// (0x0073BDA0 to 0x0073BDF0).
//
// Calls the code makes: the manager's own slots 15 to 20 for each ID of a
// group (ret 4 or 8, as the slots themselves); the terrain's (+0x88) slot
// 0x110 (four arguments, from the caller's stack: 0, the water vertices
// flag, the rectangle, 0); the message's slot 4 (none); cIGZString's slot
// 0x6C (Sprintf, cdecl); cRZBaseUnknown::QueryInterface1 0x009457C6 (ret 8,
// this + 8); SC4Rect::Intersect 0x0048ED90 (ret 8); 0x0073A1B0 (ret 8); the
// allocator 0x0090CF54 and its free 0x0090CF63 (cdecl).
//
// Every caller in the exe calls these through the vtable or with plain
// thiscall/cdecl frames and keeps nothing in EAX, ECX or EDX across the call;
// no jump lands inside the first five bytes of any of them.
//
// Compare mode: a call from the game's code runs ours on a copy of the
// manager (the records the call can reach, the bucket lists that may lose
// nodes copied, the others shared; a stand-in terrain that records the
// redraws), then the game's on the manager itself (with a stand-in terrain
// too, whose redraws are then passed on to the real one), and compares the
// two: the fields, the reached records, every bucket, the redraws and the
// result. Inside such a call each side calls its own: ours reach ours, the
// game's the game's (part 2's functions too, the side being shared through
// overlay_manage.h). The getters are compared as pure functions.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/overlay_manage.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <type_traits>
#include <utility>
#include <string>
#include <vector>

namespace opensc4::overlay_manage {

Side g_side = Side::None;

namespace {

// The manager.
constexpr std::size_t kSize = 0xFC;
constexpr std::size_t kWaterRows = 0x15;     // bool: draws the water's rows (no buckets)
constexpr std::size_t kWaterVertices = 0x16; // bool: draws the water vertices
constexpr std::size_t kClip = 0x78;          // int32[4]: the terrain's cells (0, 0, width, height)
constexpr std::size_t kTerrain = 0x88;
constexpr std::size_t kOverlays = 0x98;
constexpr std::size_t kOverlayFirstFree = 0xA4, kOverlayLastFree = 0xA8;
constexpr std::size_t kHandles = 0xAC;      // the handles' vector; its first and last free at +0xC, +0x10
constexpr std::size_t kGroups = 0xC0;
constexpr std::size_t kGroupFirstFree = 0xCC, kGroupLastFree = 0xD0;
constexpr std::size_t kDecalsDrawn = 0xD8, kShadowsDrawn = 0xDC;
constexpr std::size_t kBucketShift = 0xE4, kBucketColumns = 0xE8;
constexpr std::size_t kBuckets = 0xF0;
constexpr std::size_t kVectors[] = {kOverlays, kHandles, kGroups, kBuckets};

// The records.
constexpr std::size_t kOverlaySize = 0xB4, kHandleSize = 0x30, kGroupSize = 0x24;
constexpr std::size_t kFlags = 0x4, kDrawMode = 0x8, kCells = 0xC, kMatrix = 0x1C, kAlpha = 0x9C, kColour = 0xA0;
constexpr std::size_t kDecalInfo = 0x4, kTarget = 0x2C; // a handle's
constexpr std::size_t kGroupIds = 0x4;
constexpr int kGroupCount = 8;

constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr uint32_t kHandleBit = 0x80000000;
constexpr uint32_t kIndexMask = 0x7FFFFFFF;
constexpr uint32_t kIidOverlayManager = 0x895D6544;
constexpr uint32_t kMessageOptions = 0xC9F775BB; // the message that has the manager read the options again

// Byte offsets of the manager's own slots.
constexpr std::size_t kSlotRemove = 0x3C, kSlotSetEnabled = 0x40, kSlotSetAlpha = 0x44, kSlotSetColour = 0x48,
                      kSlotSetDrawMode = 0x4C, kSlotSetFlags = 0x50;
constexpr std::size_t kTerrainRedraw = 0x110;

// The constants the code reads (0x00A81054, 0x00A81228, 0x00AB39A0,
// 0x00AB3910, 0x00A84D2C, 0x00AB390C).
constexpr float kZero = 0.0f, kOne = 1.0f;
constexpr double kAlphaStep = 0.05;
constexpr float kMoveEpsilon = 9.99999974737875e-05f; // 0x38D1B717
constexpr double kHalf = 0.5, kRoundBias = 12582912.0; // 1.5 * 2^23: a float this size has no fraction bits

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

bool g_compare = false;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("overlays manage1: first %s", name);
}

char* Overlay(char* self, uint32_t index)
{
    return At<char*>(self, kOverlays) + index * kOverlaySize;
}

char* Handle(char* self, uint32_t id)
{
    return At<char*>(self, kHandles) + (id & kIndexMask) * kHandleSize;
}

char* Group(char* self, uint32_t target)
{
    return At<char*>(self, kGroups) + (target & kIndexMask) * kGroupSize;
}

// An ID to the overlay the getters read: a handle's target, a group's first ID.
uint32_t Resolve(char* self, uint32_t id)
{
    if (static_cast<int32_t>(id) < 0) {
        id = At<uint32_t>(Handle(self, id), kTarget);
        if (static_cast<int32_t>(id) < 0)
            id = At<uint32_t>(Group(self, id), kGroupIds);
    }
    return id;
}

// The setters' start: a handle whose target is a group has the manager's
// slot `slot` called for each of the group's IDs (true: nothing more to do);
// else `id` becomes the overlay's index.
bool PassToGroup(char* self, uint32_t& id, std::size_t slot, uint32_t value)
{
    if (static_cast<int32_t>(id) >= 0)
        return false;
    const uint32_t target = At<uint32_t>(Handle(self, id), kTarget);
    if (static_cast<int32_t>(target) >= 0) {
        id = target;
        return false;
    }
    const uint32_t* ids = &At<uint32_t>(Group(self, target), kGroupIds);
    for (int i = 0; i < kGroupCount; ++i)
        Virtual<void>(self, slot, ids[i], value);
    return true;
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// ====================================================================================
// Ours
// ====================================================================================

// 0x007352D0, cdecl: true when a position (x, z) moved by 1e-4 or more on
// either axis from another (the differences on the x87, compared with the
// float).
uint32_t __cdecl Moved(const float* a, const float* b)
{
    if (!(std::fabs(static_cast<double>(a[0]) - static_cast<double>(b[0])) < static_cast<double>(kMoveEpsilon)))
        return 1;
    if (!(std::fabs(static_cast<double>(a[1]) - static_cast<double>(b[1])) < static_cast<double>(kMoveEpsilon)))
        return 1;
    return 0;
}

// The rounding of 0x00735380: v + bias stored as a float, its bits less the
// bias's, is v rounded to the nearest integer (ties to even).
int32_t RoundedBits(double sum)
{
    return static_cast<int32_t>(Bits(static_cast<float>(sum)) - 0x4B400000u);
}

// 0x00735380, cdecl (float[4] x0 z0 x1 z1, int32[4] out): the cells a
// rectangle in cell units covers. The low corner is v - 0.5 rounded, or one
// more when v is that integer; the high corner v + 0.5 rounded, or one less
// when v is that integer.
void __cdecl CellRect(const float* rect, int32_t* out)
{
    for (int i = 0; i < 2; ++i) {
        const double v = rect[i];
        const int32_t n = RoundedBits((v - kHalf) + kRoundBias);
        out[i] = v == static_cast<double>(n + 1) ? n + 1 : n;
    }
    for (int i = 2; i < 4; ++i) {
        const double v = rect[i];
        const int32_t n = RoundedBits((kHalf + v) + kRoundBias);
        out[i] = v == static_cast<double>(n - 1) ? n - 1 : n;
    }
}

// 0x00737D40, ret 4: the terrain redraws an overlay's cells (x0 z0 x1 z1,
// the ends excluded) unless they are empty.
void __fastcall Invalidate(char* self, void*, uint32_t index)
{
    const int32_t* cells = &At<int32_t>(Overlay(self, index), kCells);
    int32_t rect[4] = {cells[0], cells[1], cells[2], cells[3]};
    if (rect[2] <= rect[0] || rect[3] <= rect[1])
        return;
    Virtual<void>(At<void*>(self, kTerrain), kTerrainRedraw, 0u, static_cast<uint32_t>(At<uint8_t>(self, kWaterVertices)),
                  rect, 0u);
}

// The bucket range of a cell rectangle: the low corner shifted, the high one
// rounded up.
struct BucketRange {
    int32_t x0, z0, x1, z1;
};

BucketRange Buckets(char* self, const int32_t* cells)
{
    const int shift = At<int32_t>(self, kBucketShift) & 31;
    const int32_t round = (1 << shift) - 1;
    return {cells[0] >> shift, cells[1] >> shift, (cells[2] + round) >> shift, (cells[3] + round) >> shift};
}

uint32_t** Bucket(char* self, int32_t x, int32_t z)
{
    return At<uint32_t**>(self, kBuckets) + (At<int32_t>(self, kBucketColumns) * z + x);
}

// 0x00738230, ret 8 (index, int32[4] cells): the overlay put at the front of
// every bucket the cells touch, column by column.
void __fastcall BucketInsert(char* self, void*, uint32_t index, const int32_t* cells)
{
    const BucketRange r = Buckets(self, cells);
    for (int32_t x = r.x0; x < r.x1; ++x)
        for (int32_t z = r.z0; z < r.z1; ++z) {
            uint32_t** const head = Bucket(self, x, z);
            uint32_t* const node = Cdecl<uint32_t*>(addr::SC4_stl_allocate, std::size_t{8});
            node[1] = index;
            node[0] = 0;
            node[0] = reinterpret_cast<uint32_t>(*head);
            *head = node;
        }
}

// 0x007382E0, ret 8 (index, int32[4] cells): the overlay taken out of every
// bucket the cells touch, row by row, each node of it freed.
void __fastcall BucketRemove(char* self, void*, uint32_t index, const int32_t* cells)
{
    const BucketRange r = Buckets(self, cells);
    for (int32_t z = r.z0; z < r.z1; ++z)
        for (int32_t x = r.x0; x < r.x1; ++x) {
            uint32_t** link = Bucket(self, x, z);
            while (link) {
                uint32_t* const node = *link;
                if (!node)
                    break;
                if (node[1] == index) {
                    *link = reinterpret_cast<uint32_t*>(node[0]);
                    Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
                } else {
                    link = reinterpret_cast<uint32_t**>(node);
                }
            }
        }
}

// 0x00738A10, ret 4: an overlay put into the buckets and redrawn (not -1,
// not for the water's rows).
void __fastcall Place(char* self, void*, uint32_t index)
{
    if (index == kNone || At<uint8_t>(self, kWaterRows))
        return;
    BucketInsert(self, nullptr, index, &At<int32_t>(Overlay(self, index), kCells));
    Invalidate(self, nullptr, index);
}

// 0x007393B0, ret 8 (index, int32[4] cells): the overlay's cells become the
// given ones cut to the terrain (nothing when they miss it); when that
// changes them it leaves the old buckets and is redrawn there first. Redrawn
// at the end either way.
void __fastcall SetCells(char* self, void*, uint32_t index, const int32_t* cells)
{
    char* const overlay = Overlay(self, index);
    int32_t cut[4];
    This<uint8_t>(addr::SC4Rect_Intersect, cut, cells, &At<int32_t>(self, kClip));
    int32_t* const now = &At<int32_t>(overlay, kCells);
    if (cut[0] != now[0] || cut[1] != now[1] || cut[2] != now[2] || cut[3] != now[3]) {
        BucketRemove(self, nullptr, index, now);
        Invalidate(self, nullptr, index);
        std::memcpy(now, cut, sizeof cut);
        BucketInsert(self, nullptr, index, now);
    }
    Invalidate(self, nullptr, index);
}

// 0x00736210, ret 4, this = the handles' vector (the manager + 0xAC): a
// handle in use put at the end of the free list.
void __fastcall FreeHandle(char* handles, void*, uint32_t index)
{
    char* const handle = At<char*>(handles, 0) + index * kHandleSize;
    if (At<int32_t>(handle, 0) != -1)
        return;
    At<uint32_t>(handle, 0) = 0;
    const int32_t last = At<int32_t>(handles, 0x10);
    if (last != -1) {
        At<uint32_t>(At<char*>(handles, 0) + last * kHandleSize, 0) = index;
        At<uint32_t>(handles, 0x10) = index;
        return;
    }
    At<uint32_t>(handles, 0xC) = index;
    At<uint32_t>(handles, 0x10) = index;
}

// Slot 15, 0x007388C0, ret 4: RemoveOverlay. A handle is freed, then its
// target removed: a group's eight IDs each through the slot (the group then
// freed), an overlay out of its buckets (redrawn there) and freed.
void __fastcall RemoveOverlay(char* self, void*, uint32_t id)
{
    if (id == kNone)
        return;
    uint32_t index = id;
    if (static_cast<int32_t>(id) < 0) {
        const uint32_t handle = id & kIndexMask;
        index = At<uint32_t>(Handle(self, id), kTarget);
        FreeHandle(self + kHandles, nullptr, handle);
        if (static_cast<int32_t>(index) < 0) {
            const uint32_t group = index & kIndexMask;
            const uint32_t* ids = &At<uint32_t>(Group(self, index), kGroupIds);
            for (int i = 0; i < kGroupCount; ++i)
                Virtual<void>(self, kSlotRemove, ids[i]);
            char* const record = At<char*>(self, kGroups) + group * kGroupSize;
            if (At<int32_t>(record, 0) != -1)
                return;
            At<uint32_t>(record, 0) = 0;
            const int32_t last = At<int32_t>(self, kGroupLastFree);
            if (last != -1) {
                At<uint32_t>(At<char*>(self, kGroups) + last * kGroupSize, 0) = group;
                At<uint32_t>(self, kGroupLastFree) = group;
                return;
            }
            At<uint32_t>(self, kGroupFirstFree) = group;
            At<uint32_t>(self, kGroupLastFree) = group;
            return;
        }
    }
    if (!At<uint8_t>(self, kWaterRows)) {
        BucketRemove(self, nullptr, index, &At<int32_t>(Overlay(self, index), kCells));
        Invalidate(self, nullptr, index);
    }
    char* const overlay = Overlay(self, index);
    if (At<int32_t>(overlay, 0) != -1)
        return;
    At<uint32_t>(overlay, 0) = 0;
    const int32_t last = At<int32_t>(self, kOverlayLastFree);
    if (last != -1) {
        At<uint32_t>(Overlay(self, last), 0) = index;
        At<uint32_t>(self, kOverlayLastFree) = index;
        return;
    }
    At<uint32_t>(self, kOverlayFirstFree) = index;
    At<uint32_t>(self, kOverlayLastFree) = index;
}

// Slot 16, 0x00737E50, ret 8 (id, bool): flag 1 set or cleared; redrawn.
void __fastcall SetOverlayEnabled(char* self, void*, uint32_t id, uint32_t enabled)
{
    if (id == kNone || PassToGroup(self, id, kSlotSetEnabled, enabled))
        return;
    uint32_t& flags = At<uint32_t>(Overlay(self, id), kFlags);
    flags = (enabled & 0xFF) ? flags | 1 : flags & ~1u;
    if (!At<uint8_t>(self, kWaterRows))
        Invalidate(self, nullptr, id);
}

// Slot 17, 0x00737F00, ret 8 (id, alpha): kept as it is for the water's
// rows; otherwise only a change to 0, to 1 or by more than 0.05 is kept,
// and redrawn.
void __fastcall SetOverlayAlpha(char* self, void*, uint32_t id, float alpha)
{
    if (id == kNone || PassToGroup(self, id, kSlotSetAlpha, Bits(alpha)))
        return;
    float& kept = At<float>(Overlay(self, id), kAlpha);
    if (At<uint8_t>(self, kWaterRows)) {
        kept = alpha;
        return;
    }
    const float old = kept;
    if (old == alpha)
        return;
    if (!(alpha == kZero || alpha == kOne)
        && !(std::fabs(static_cast<double>(alpha) - static_cast<double>(old)) > kAlphaStep))
        return;
    kept = alpha;
    Invalidate(self, nullptr, id);
}

// Slot 18, 0x00738010, ret 8 (id, float[3]): the colour; redrawn.
void __fastcall SetOverlayColor(char* self, void*, uint32_t id, const uint32_t* colour)
{
    if (id == kNone || PassToGroup(self, id, kSlotSetColour, reinterpret_cast<uint32_t>(colour)))
        return;
    uint32_t* const kept = &At<uint32_t>(Overlay(self, id), kColour);
    kept[0] = colour[0];
    kept[1] = colour[1];
    kept[2] = colour[2];
    if (!At<uint8_t>(self, kWaterRows))
        Invalidate(self, nullptr, id);
}

// Slot 19, 0x007380C0, ret 8 (id, byte): the draw mode, the index of the
// overlay's render state; redrawn.
void __fastcall SetOverlayDrawMode(char* self, void*, uint32_t id, uint32_t mode)
{
    if (id == kNone || PassToGroup(self, id, kSlotSetDrawMode, mode))
        return;
    At<uint8_t>(Overlay(self, id), kDrawMode) = static_cast<uint8_t>(mode);
    if (!At<uint8_t>(self, kWaterRows))
        Invalidate(self, nullptr, id);
}

// Slot 20, 0x007364F0, ret 8 (id, flags): with bit 0 of the argument the
// overlay's flag 0x40 (its colour set the second way) is set; it is never
// cleared here, and nothing is redrawn.
void __fastcall SetOverlayFlags(char* self, void*, uint32_t id, uint32_t flags)
{
    if (id == kNone || PassToGroup(self, id, kSlotSetFlags, flags))
        return;
    if (flags & 1)
        At<uint32_t>(Overlay(self, id), kFlags) |= 0x40;
}

// Slot 21, 0x00738160, ret 0xC (id, scale, offset): flag 0x20 and the
// texture matrix of a height map: 1 / scale on the diagonal (its negative
// second), 1 in the third row's third column and the last, and
// 1 / scale * offset + 1 as the fourth row's second value (on the x87, then
// stored); redrawn. A handle's target is taken as it is.
void __fastcall SetHeightMapParams(char* self, void*, uint32_t id, float scale, float offset)
{
    if (id == kNone)
        return;
    if (static_cast<int32_t>(id) < 0)
        id = At<uint32_t>(Handle(self, id), kTarget);
    const double inverse = static_cast<double>(kOne) / static_cast<double>(scale);
    const float shift = static_cast<float>(inverse * static_cast<double>(offset) + static_cast<double>(kOne));
    char* const overlay = Overlay(self, id);
    At<uint32_t>(overlay, kFlags) |= 0x20;
    float* const m = &At<float>(Overlay(self, id), kMatrix);
    const float diagonal = static_cast<float>(inverse);
    const float negative = static_cast<float>(-inverse);
    m[1] = 0.0f;
    m[0] = diagonal;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[4] = 0.0f;
    m[5] = negative;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[9] = 0.0f;
    m[8] = diagonal;
    m[10] = 1.0f;
    m[11] = 0.0f;
    m[13] = shift;
    m[12] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
    if (!At<uint8_t>(self, kWaterRows))
        Invalidate(self, nullptr, id);
}

// Slot 22, 0x00736580, ret 4: flag 1 (EAX all ones but AL for -1).
uint32_t __fastcall OverlayIsEnabled(char* self, void*, uint32_t id)
{
    if (id == kNone)
        return 0xFFFFFF00;
    return At<uint32_t>(Overlay(self, Resolve(self, id)), kFlags) & 1;
}

// Slot 23, 0x007365E0, ret 4: the alpha (0 for -1).
float __fastcall OverlayAlpha(char* self, void*, uint32_t id)
{
    if (id == kNone)
        return kZero;
    return At<float>(Overlay(self, Resolve(self, id)), kAlpha);
}

// Slot 24, 0x00736640, ret 8 (out float[3], id): the colour by value (0, 0,
// 0 for -1); returns out.
uint32_t* __fastcall OverlayColor(char* self, void*, uint32_t* out, uint32_t id)
{
    if (id == kNone) {
        out[0] = out[1] = out[2] = 0;
        return out;
    }
    const uint32_t* colour = &At<uint32_t>(Overlay(self, Resolve(self, id)), kColour);
    out[0] = colour[0];
    out[1] = colour[1];
    out[2] = colour[2];
    return out;
}

// Slot 25, 0x007366C0, ret 4: the draw mode (0 for -1).
uint32_t __fastcall OverlayDrawMode(char* self, void*, uint32_t id)
{
    if (id == kNone)
        return 0;
    return At<uint8_t>(Overlay(self, Resolve(self, id)), kDrawMode);
}

// Slot 26, 0x00736720, ret 4: flag 0x40 (0 for -1).
uint32_t __fastcall OverlayFlags(char* self, void*, uint32_t id)
{
    if (id == kNone)
        return 0;
    return (At<uint32_t>(Overlay(self, Resolve(self, id)), kFlags) >> 6) & 1;
}

// Slot 14, 0x00736490, ret 8 (id, out): a handle's decal description (nine
// values); nothing for an overlay's ID.
void __fastcall DecalInfo(char* self, void*, uint32_t id, uint32_t* out)
{
    if (id == kNone || static_cast<int32_t>(id) >= 0)
        return;
    std::memcpy(out, &At<uint32_t>(Handle(self, id), kDecalInfo), 9 * sizeof(uint32_t));
}

// Slot 12, 0x0073AD80, ret 8 (id, float[2] position): a handle's decal
// moved there (when it moved, 0x007352D0) and built again (0x0073A1B0 with
// the handle and whether the manager has buckets; ours when manage2 is on).
void __fastcall MoveDecal(char* self, void*, uint32_t id, const float* position)
{
    if (id == kNone || static_cast<int32_t>(id) >= 0)
        return;
    char* const handle = Handle(self, id);
    float* const centre = &At<float>(handle, kDecalInfo);
    if (!Moved(position, centre))
        return;
    std::memcpy(centre, position, 2 * sizeof(float));
    This<void>(addr::cSC4OverlayManager_BuildDecal, self, handle, static_cast<uint32_t>(At<uint8_t>(self, kWaterRows) == 0));
}

// Slot 13, 0x0073ADE0, ret 8 (id, nine values): a handle's decal described
// anew and built again.
void __fastcall UpdateDecalInfo(char* self, void*, uint32_t id, const uint32_t* info)
{
    if (id == kNone || static_cast<int32_t>(id) >= 0)
        return;
    char* const handle = Handle(self, id);
    std::memcpy(&At<uint32_t>(handle, kDecalInfo), info, 9 * sizeof(uint32_t));
    This<void>(addr::cSC4OverlayManager_BuildDecal, self, handle, static_cast<uint32_t>(At<uint8_t>(self, kWaterRows) == 0));
}

// Slot 27, 0x007352A0, ret 4 (cIGZString*): "%d decals, %d shadows" with the
// last draw's counts.
void __fastcall GetStatsString(char* self, void*, void* text)
{
    using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
    const auto format = reinterpret_cast<SprintfFn>((*static_cast<void***>(text))[0x6C / 4]);
    format(text, "%d decals, %d shadows", At<int32_t>(self, kDecalsDrawn), At<int32_t>(self, kShadowsDrawn));
}

// Slot 0, 0x00735260, ret 8 (iid, out): the overlay manager's interface is
// the object itself (AddRef'd); anything else goes to the base's at +8.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kIidOverlayManager)
        return This<uint32_t>(addr::cRZBaseUnknown_QueryInterface1, self + 8, iid, out);
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// Slot 3 of the message target at +0x10 (0x00736100, ret 4, this = the
// manager + 0x10): on 0xC9F775BB the driver's stage count (+0x1FC) and
// +0xBC into +0x18 and +0x1C and an option byte (+0x37C) into +0x20, from the
// object at 0x00B43CC4 (+0x18, +0xC). True.
uint32_t __fastcall DoMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == kMessageOptions) {
        char* const app = Global<char*>(addr::SC4_App43CC4);
        At<uint32_t>(target, 0x8) = At<uint32_t>(At<char*>(app, 0x18), 0x1FC);
        At<uint32_t>(target, 0xC) = At<uint32_t>(At<char*>(app, 0x18), 0xBC);
        At<uint8_t>(target, 0x10) = At<uint8_t>(At<char*>(app, 0xC), 0x37C);
    }
    return 1;
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
    Redraw r{first, water, {rect[0], rect[1], rect[2], rect[3]}, last};
    self->redraws->push_back(r);
}

void* g_standInVtable[kTerrainRedraw / 4 + 1] = {};

std::string Hex32(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof text, "%08X", static_cast<unsigned>(value));
    return text;
}

// What a call can reach: the records it may read or write (an ID's handle,
// its overlay or its group and the group's IDs, and for a removal the last
// record of each free list), and the cells whose buckets may lose nodes
// (each reached overlay's cells, and a removal's own).
struct Reach {
    std::vector<uint32_t> records[3];
    std::vector<std::array<int32_t, 4>> removals;
};

uint32_t Count(char* self, int v)
{
    static const std::size_t kRecordSizes[] = {kOverlaySize, kHandleSize, kGroupSize};
    return static_cast<uint32_t>((At<char*>(self, kVectors[v] + 4) - At<char*>(self, kVectors[v])) / kRecordSizes[v]);
}

void ReachOverlay(char* self, uint32_t index, Reach& reach)
{
    if (index >= Count(self, 0))
        return;
    reach.records[0].push_back(index);
    const int32_t* cells = &At<int32_t>(Overlay(self, index), kCells);
    reach.removals.push_back({cells[0], cells[1], cells[2], cells[3]});
}

void ReachId(char* self, uint32_t id, Reach& reach, int depth = 0)
{
    if (id == kNone)
        return;
    if (static_cast<int32_t>(id) >= 0) {
        ReachOverlay(self, id, reach);
        return;
    }
    const uint32_t handle = id & kIndexMask;
    if (handle >= Count(self, 1))
        return;
    reach.records[1].push_back(handle);
    const uint32_t target = At<uint32_t>(Handle(self, id), kTarget);
    if (static_cast<int32_t>(target) >= 0) {
        ReachOverlay(self, target, reach);
        return;
    }
    const uint32_t group = target & kIndexMask;
    if (group >= Count(self, 2))
        return;
    reach.records[2].push_back(group);
    if (depth < 2)
        for (int i = 0; i < kGroupCount; ++i)
            ReachId(self, At<uint32_t>(Group(self, target), kGroupIds + 4 * i), reach, depth + 1);
}

void ReachFreeLists(char* self, Reach& reach)
{
    for (const auto& [v, last] : {std::pair{0, kOverlayLastFree}, std::pair{1, kHandles + 0x10}, std::pair{2, kGroupLastFree}}) {
        const uint32_t index = At<uint32_t>(self, last);
        if (index < Count(self, v))
            reach.records[v].push_back(index);
    }
}

Reach ReachOf(char* self, uint32_t id, bool removal = false)
{
    Reach reach;
    ReachId(self, id, reach);
    if (removal)
        ReachFreeLists(self, reach);
    return reach;
}

Reach ReachCells(char* self, uint32_t index, const int32_t* cells)
{
    Reach reach = ReachOf(self, index);
    reach.removals.push_back({cells[0], cells[1], cells[2], cells[3]});
    return reach;
}

// A copy of a manager for ours: the manager itself, the records the call can
// reach (in buffers the size of the vectors, at the same places; the other
// records are not copied) and the bucket heads. The buckets that may lose
// nodes get copies of their lists (nodes from the game's allocator, which
// ours frees and takes from); the others share the real lists, onto which
// ours can only push.
class Shadow {
public:
    Shadow(char* real, const Reach& reach) : real_(real), reach_(reach)
    {
        static const std::size_t kRecordSizes[] = {kOverlaySize, kHandleSize, kGroupSize};
        std::memcpy(object_, real, kSize);
        for (int v = 0; v < 3; ++v) {
            char* const begin = At<char*>(real, kVectors[v]);
            const std::size_t used = At<char*>(real, kVectors[v] + 4) - begin;
            const std::size_t capacity = At<char*>(real, kVectors[v] + 8) - begin;
            if (Buffer(v).size() < capacity)
                Buffer(v).resize(capacity);
            char* const copy = begin ? Buffer(v).data() : nullptr;
            for (uint32_t index : reach.records[v])
                std::memcpy(copy + index * kRecordSizes[v], begin + index * kRecordSizes[v], kRecordSizes[v]);
            At<char*>(object_, kVectors[v]) = copy;
            At<char*>(object_, kVectors[v] + 4) = copy + used;
            At<char*>(object_, kVectors[v] + 8) = copy + capacity;
        }
        uint32_t** const begin = At<uint32_t**>(real, kBuckets);
        count_ = At<uint32_t**>(real, kBuckets + 4) - begin;
        const std::size_t capacity = At<uint32_t**>(real, kBuckets + 8) - begin;
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
                    const std::size_t i = static_cast<std::size_t>(At<int32_t>(real, kBucketColumns) * z + x);
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
        At<uint32_t**>(object_, kBuckets + 4) = shadowHeads + count_;
        At<uint32_t**>(object_, kBuckets + 8) = shadowHeads + capacity;
    }

    // Our nodes freed: a copied list whole, a shared one up to the real
    // list's head it started from.
    ~Shadow()
    {
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
        for (int v = 0; v < 4; ++v) {
            const char* const begin = At<char*>(real_, kVectors[v]);
            const char* const end = At<char*>(real_, kVectors[v] + 8);
            if (begin && p >= begin && p < end)
                return reinterpret_cast<T*>(At<char*>(const_cast<char*>(object_), kVectors[v]) + (p - begin));
        }
        return pointer;
    }

    // The first difference between the real manager and the copy, or empty:
    // the manager's fields (the vectors by size), the reached records, every
    // bucket (a shared list both left alone has the same head).
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
        static const char* const kNames[] = {"overlay", "handle", "group"};
        static const std::size_t kRecordSizes[] = {kOverlaySize, kHandleSize, kGroupSize};
        for (int v = 0; v < 4; ++v)
            for (std::size_t end : {std::size_t{4}, std::size_t{8}}) {
                const std::size_t a = At<char*>(real_, kVectors[v] + end) - At<char*>(real_, kVectors[v]);
                const std::size_t b = At<char*>(ours, kVectors[v] + end) - At<char*>(ours, kVectors[v]);
                if (a != b)
                    return Field(kVectors[v] + end, static_cast<uint32_t>(a), static_cast<uint32_t>(b));
            }
        for (int v = 0; v < 3; ++v)
            for (uint32_t index : reach_.records[v]) {
                const char* const a = At<char*>(real_, kVectors[v]) + index * kRecordSizes[v];
                const char* const b = At<char*>(ours, kVectors[v]) + index * kRecordSizes[v];
                for (std::size_t at = 0; at < kRecordSizes[v]; at += 4)
                    if (std::memcmp(a + at, b + at, 4) != 0) {
                        char where[48];
                        std::snprintf(where, sizeof where, "%s %u +0x%X: ", kNames[v], static_cast<unsigned>(index),
                                      static_cast<unsigned>(at));
                        uint32_t x, y;
                        std::memcpy(&x, a + at, 4);
                        std::memcpy(&y, b + at, 4);
                        return std::string(where) + Hex32(x) + " / " + Hex32(y);
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
    std::size_t count_ = 0;
    std::set<std::size_t> copied_;
};

template <typename T>
uint32_t ResultBits(const T& value)
{
    if constexpr (std::is_same_v<T, float>)
        return Bits(value);
    else if constexpr (std::is_pointer_v<T>)
        return reinterpret_cast<uint32_t>(value);
    else
        return static_cast<uint32_t>(value);
}

uint64_t g_stateCompared = 0;

// A call that changes the manager, compared: ours on a copy, the game's on
// the manager. `ours` is given the copy (and maps its pointer arguments with
// it); `game` runs the original. Both with stand-in terrains; the game's
// redraws are passed on to the real terrain afterwards.
template <typename Ours, typename Game>
auto CompareState(const char* id, char* self, const Reach& reach, Ours&& ours, Game&& game)
{
    Shadow shadow(self, reach);
    std::vector<Redraw> ourRedraws, gameRedraws;
    StandInTerrain ourTerrain{g_standInVtable, &ourRedraws}, gameTerrain{g_standInVtable, &gameRedraws};
    At<void*>(shadow.self(), kTerrain) = &ourTerrain;
    void* const terrain = At<void*>(self, kTerrain);
    using R = decltype(game());
    std::string diff;
    const auto finish = [&] {
        g_side = Side::None;
        At<void*>(self, kTerrain) = terrain;
        for (Redraw& r : gameRedraws)
            Virtual<void>(terrain, kTerrainRedraw, r.first, r.water, r.rect, r.last);
        diff = shadow.Diff();
        if (diff.empty() && ourRedraws != gameRedraws) {
            std::size_t at = 0;
            while (at < ourRedraws.size() && at < gameRedraws.size() && ourRedraws[at] == gameRedraws[at])
                ++at;
            diff = "redraw " + std::to_string(at) + " of " + std::to_string(gameRedraws.size()) + " / "
                   + std::to_string(ourRedraws.size());
        }
    };
    if constexpr (std::is_void_v<R>) {
        g_side = Side::Ours;
        ours(shadow);
        At<void*>(self, kTerrain) = &gameTerrain;
        g_side = Side::Game;
        game();
        finish();
        compare::Record(id, diff.empty(), diff, diff);
        ++g_stateCompared;
        if ((g_stateCompared & (g_stateCompared - 1)) == 0)
            log::Write("overlays manage1: %llu calls compared", static_cast<unsigned long long>(g_stateCompared));
    } else {
        g_side = Side::Ours;
        const R mine = ours(shadow);
        At<void*>(self, kTerrain) = &gameTerrain;
        g_side = Side::Game;
        const R result = game();
        finish();
        if (diff.empty() && ResultBits(result) != ResultBits(mine))
            diff = "result " + Hex32(ResultBits(result)) + " / " + Hex32(ResultBits(mine));
        compare::Record(id, diff.empty(), diff, diff);
        ++g_stateCompared;
        if ((g_stateCompared & (g_stateCompared - 1)) == 0)
            log::Write("overlays manage1: %llu calls compared", static_cast<unsigned long long>(g_stateCompared));
        return result;
    }
}

// A getter compared: the game's, then ours, the results' bits.
template <typename Ours, typename Game>
auto ComparePure(const char* id, Ours&& ours, Game&& game)
{
    g_side = Side::Game;
    const auto result = game();
    g_side = Side::Ours;
    const auto mine = ours();
    g_side = Side::None;
    const uint32_t a = ResultBits(result), b = ResultBits(mine);
    compare::Record(id, a == b, Hex32(a), Hex32(b));
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

// ====================================================================================
// Detours
// ====================================================================================

using IdFn = void(__fastcall*)(char*, void*, uint32_t);
using IdValueFn = void(__fastcall*)(char*, void*, uint32_t, uint32_t);
using IdFloatFn = void(__fastcall*)(char*, void*, uint32_t, float);
using IdPointerFn = void(__fastcall*)(char*, void*, uint32_t, const void*);
using IdCellsFn = void(__fastcall*)(char*, void*, uint32_t, const int32_t*);
using HeightMapFn = void(__fastcall*)(char*, void*, uint32_t, float, float);
using GetFn = uint32_t(__fastcall*)(char*, void*, uint32_t);
using GetFloatFn = float(__fastcall*)(char*, void*, uint32_t);
using ColourFn = uint32_t*(__fastcall*)(char*, void*, uint32_t*, uint32_t);
using InfoFn = void(__fastcall*)(char*, void*, uint32_t, uint32_t*);
using TextFn = void(__fastcall*)(char*, void*, void*);
using QueryFn = uint32_t(__fastcall*)(char*, void*, uint32_t, void**);
using MessageFn = uint32_t(__fastcall*)(char*, void*, void*);
using MovedFn = uint32_t(__cdecl*)(const float*, const float*);
using CellRectFn = void(__cdecl*)(const float*, int32_t*);

IdFn g_invalidate, g_place, g_freeHandle, g_remove;
IdCellsFn g_bucketInsert, g_bucketRemove, g_setCells;
IdValueFn g_setEnabled, g_setDrawMode, g_setFlags;
IdFloatFn g_setAlpha;
IdPointerFn g_setColour, g_moveDecal, g_updateDecalInfo;
HeightMapFn g_setHeightMap;
GetFn g_isEnabled, g_drawMode, g_flags;
GetFloatFn g_alpha;
ColourFn g_colour;
InfoFn g_decalInfo;
TextFn g_stats;
QueryFn g_query;
MessageFn g_message;
MovedFn g_moved;
CellRectFn g_cellRect;

#define REACHED(name)                                                                                                  \
    static bool reached = false;                                                                                       \
    FirstCall(name, reached)

void __fastcall DInvalidate(char* self, void*, uint32_t index)
{
    REACHED("Invalidate");
    Route([&] { Invalidate(self, nullptr, index); }, [&] { g_invalidate(self, nullptr, index); },
          [&] {
              CompareState("renderer.overlays.manage1.invalidate", self, ReachOf(self, index),
                           [&](Shadow& s) { Invalidate(s.self(), nullptr, index); }, [&] { g_invalidate(self, nullptr, index); });
          });
}

void __fastcall DBucketInsert(char* self, void*, uint32_t index, const int32_t* cells)
{
    REACHED("BucketInsert");
    Route([&] { BucketInsert(self, nullptr, index, cells); }, [&] { g_bucketInsert(self, nullptr, index, cells); },
          [&] {
              CompareState("renderer.overlays.manage1.bucketinsert", self, ReachOf(self, index),
                           [&](Shadow& s) { BucketInsert(s.self(), nullptr, index, s.Map(cells)); },
                           [&] { g_bucketInsert(self, nullptr, index, cells); });
          });
}

void __fastcall DBucketRemove(char* self, void*, uint32_t index, const int32_t* cells)
{
    REACHED("BucketRemove");
    Route([&] { BucketRemove(self, nullptr, index, cells); }, [&] { g_bucketRemove(self, nullptr, index, cells); },
          [&] {
              CompareState("renderer.overlays.manage1.bucketremove", self, ReachCells(self, index, cells),
                           [&](Shadow& s) { BucketRemove(s.self(), nullptr, index, s.Map(cells)); },
                           [&] { g_bucketRemove(self, nullptr, index, cells); });
          });
}

void __fastcall DPlace(char* self, void*, uint32_t index)
{
    REACHED("Place");
    Route([&] { Place(self, nullptr, index); }, [&] { g_place(self, nullptr, index); },
          [&] {
              CompareState("renderer.overlays.manage1.place", self, ReachOf(self, index), [&](Shadow& s) { Place(s.self(), nullptr, index); },
                           [&] { g_place(self, nullptr, index); });
          });
}

void __fastcall DSetCells(char* self, void*, uint32_t index, const int32_t* cells)
{
    REACHED("SetCells");
    Route([&] { SetCells(self, nullptr, index, cells); }, [&] { g_setCells(self, nullptr, index, cells); },
          [&] {
              CompareState("renderer.overlays.manage1.setcells", self, ReachOf(self, index),
                           [&](Shadow& s) { SetCells(s.self(), nullptr, index, s.Map(cells)); },
                           [&] { g_setCells(self, nullptr, index, cells); });
          });
}

void __fastcall DFreeHandle(char* handles, void*, uint32_t index)
{
    REACHED("FreeHandle");
    Route([&] { FreeHandle(handles, nullptr, index); }, [&] { g_freeHandle(handles, nullptr, index); },
          [&] {
              CompareState("renderer.overlays.manage1.freehandle", handles - kHandles, ReachOf(handles - kHandles, kHandleBit | index, true),
                           [&](Shadow& s) { FreeHandle(s.self() + kHandles, nullptr, index); },
                           [&] { g_freeHandle(handles, nullptr, index); });
          });
}

void __fastcall DRemove(char* self, void*, uint32_t id)
{
    REACHED("RemoveOverlay");
    Route([&] { RemoveOverlay(self, nullptr, id); }, [&] { g_remove(self, nullptr, id); },
          [&] {
              CompareState("renderer.overlays.manage1.remove", self, ReachOf(self, id, true), [&](Shadow& s) { RemoveOverlay(s.self(), nullptr, id); },
                           [&] { g_remove(self, nullptr, id); });
          });
}

void __fastcall DSetEnabled(char* self, void*, uint32_t id, uint32_t enabled)
{
    REACHED("SetOverlayEnabled");
    Route([&] { SetOverlayEnabled(self, nullptr, id, enabled); }, [&] { g_setEnabled(self, nullptr, id, enabled); },
          [&] {
              CompareState("renderer.overlays.manage1.setenabled", self, ReachOf(self, id),
                           [&](Shadow& s) { SetOverlayEnabled(s.self(), nullptr, id, enabled); },
                           [&] { g_setEnabled(self, nullptr, id, enabled); });
          });
}

void __fastcall DSetAlpha(char* self, void*, uint32_t id, float alpha)
{
    REACHED("SetOverlayAlpha");
    Route([&] { SetOverlayAlpha(self, nullptr, id, alpha); }, [&] { g_setAlpha(self, nullptr, id, alpha); },
          [&] {
              CompareState("renderer.overlays.manage1.setalpha", self, ReachOf(self, id),
                           [&](Shadow& s) { SetOverlayAlpha(s.self(), nullptr, id, alpha); },
                           [&] { g_setAlpha(self, nullptr, id, alpha); });
          });
}

void __fastcall DSetColour(char* self, void*, uint32_t id, const void* colour)
{
    REACHED("SetOverlayColor");
    const auto* c = static_cast<const uint32_t*>(colour);
    Route([&] { SetOverlayColor(self, nullptr, id, c); }, [&] { g_setColour(self, nullptr, id, colour); },
          [&] {
              CompareState("renderer.overlays.manage1.setcolour", self, ReachOf(self, id),
                           [&](Shadow& s) { SetOverlayColor(s.self(), nullptr, id, s.Map(c)); },
                           [&] { g_setColour(self, nullptr, id, colour); });
          });
}

void __fastcall DSetDrawMode(char* self, void*, uint32_t id, uint32_t mode)
{
    REACHED("SetOverlayDrawMode");
    Route([&] { SetOverlayDrawMode(self, nullptr, id, mode); }, [&] { g_setDrawMode(self, nullptr, id, mode); },
          [&] {
              CompareState("renderer.overlays.manage1.setdrawmode", self, ReachOf(self, id),
                           [&](Shadow& s) { SetOverlayDrawMode(s.self(), nullptr, id, mode); },
                           [&] { g_setDrawMode(self, nullptr, id, mode); });
          });
}

void __fastcall DSetFlags(char* self, void*, uint32_t id, uint32_t flags)
{
    REACHED("SetOverlayFlags");
    Route([&] { SetOverlayFlags(self, nullptr, id, flags); }, [&] { g_setFlags(self, nullptr, id, flags); },
          [&] {
              CompareState("renderer.overlays.manage1.setflags", self, ReachOf(self, id),
                           [&](Shadow& s) { SetOverlayFlags(s.self(), nullptr, id, flags); },
                           [&] { g_setFlags(self, nullptr, id, flags); });
          });
}

void __fastcall DSetHeightMap(char* self, void*, uint32_t id, float scale, float offset)
{
    REACHED("SetHeightMapParams");
    Route([&] { SetHeightMapParams(self, nullptr, id, scale, offset); }, [&] { g_setHeightMap(self, nullptr, id, scale, offset); },
          [&] {
              CompareState("renderer.overlays.manage1.setheightmap", self, ReachOf(self, id),
                           [&](Shadow& s) { SetHeightMapParams(s.self(), nullptr, id, scale, offset); },
                           [&] { g_setHeightMap(self, nullptr, id, scale, offset); });
          });
}

void __fastcall DMoveDecal(char* self, void*, uint32_t id, const void* position)
{
    REACHED("MoveDecal");
    const auto* p = static_cast<const float*>(position);
    Route([&] { MoveDecal(self, nullptr, id, p); }, [&] { g_moveDecal(self, nullptr, id, position); },
          [&] {
              CompareState("renderer.overlays.manage1.movedecal", self, ReachOf(self, id),
                           [&](Shadow& s) { MoveDecal(s.self(), nullptr, id, s.Map(p)); },
                           [&] { g_moveDecal(self, nullptr, id, position); });
          });
}

void __fastcall DUpdateDecalInfo(char* self, void*, uint32_t id, const void* info)
{
    REACHED("UpdateDecalInfo");
    const auto* i = static_cast<const uint32_t*>(info);
    Route([&] { UpdateDecalInfo(self, nullptr, id, i); }, [&] { g_updateDecalInfo(self, nullptr, id, info); },
          [&] {
              CompareState("renderer.overlays.manage1.updatedecalinfo", self, ReachOf(self, id),
                           [&](Shadow& s) { UpdateDecalInfo(s.self(), nullptr, id, s.Map(i)); },
                           [&] { g_updateDecalInfo(self, nullptr, id, info); });
          });
}

uint32_t __fastcall DIsEnabled(char* self, void*, uint32_t id)
{
    REACHED("OverlayIsEnabled");
    return Route([&] { return OverlayIsEnabled(self, nullptr, id); }, [&] { return g_isEnabled(self, nullptr, id); },
                 [&] {
                     return ComparePure("renderer.overlays.manage1.isenabled", [&] { return OverlayIsEnabled(self, nullptr, id); },
                                        [&] { return g_isEnabled(self, nullptr, id); });
                 });
}

float __fastcall DAlpha(char* self, void*, uint32_t id)
{
    REACHED("OverlayAlpha");
    return Route([&] { return OverlayAlpha(self, nullptr, id); }, [&] { return g_alpha(self, nullptr, id); },
                 [&] {
                     return ComparePure("renderer.overlays.manage1.alpha", [&] { return OverlayAlpha(self, nullptr, id); },
                                        [&] { return g_alpha(self, nullptr, id); });
                 });
}

uint32_t __fastcall DDrawMode(char* self, void*, uint32_t id)
{
    REACHED("OverlayDrawMode");
    return Route([&] { return OverlayDrawMode(self, nullptr, id); }, [&] { return g_drawMode(self, nullptr, id); },
                 [&] {
                     return ComparePure("renderer.overlays.manage1.drawmode", [&] { return OverlayDrawMode(self, nullptr, id); },
                                        [&] { return g_drawMode(self, nullptr, id); });
                 });
}

uint32_t __fastcall DFlags(char* self, void*, uint32_t id)
{
    REACHED("OverlayFlags");
    return Route([&] { return OverlayFlags(self, nullptr, id); }, [&] { return g_flags(self, nullptr, id); },
                 [&] {
                     return ComparePure("renderer.overlays.manage1.flags", [&] { return OverlayFlags(self, nullptr, id); },
                                        [&] { return g_flags(self, nullptr, id); });
                 });
}

uint32_t* __fastcall DColour(char* self, void*, uint32_t* out, uint32_t id)
{
    REACHED("OverlayColor");
    return Route([&] { return OverlayColor(self, nullptr, out, id); }, [&] { return g_colour(self, nullptr, out, id); },
                 [&] {
                     uint32_t mine[3] = {};
                     g_side = Side::Ours;
                     OverlayColor(self, nullptr, mine, id);
                     g_side = Side::Game;
                     uint32_t* const result = g_colour(self, nullptr, out, id);
                     g_side = Side::None;
                     const bool same = result == out && std::memcmp(out, mine, sizeof mine) == 0;
                     compare::Record("renderer.overlays.manage1.colour", same, Hex32(out[0]) + Hex32(out[1]) + Hex32(out[2]),
                                     Hex32(mine[0]) + Hex32(mine[1]) + Hex32(mine[2]));
                     return result;
                 });
}

void __fastcall DDecalInfo(char* self, void*, uint32_t id, uint32_t* out)
{
    REACHED("DecalInfo");
    Route([&] { DecalInfo(self, nullptr, id, out); }, [&] { g_decalInfo(self, nullptr, id, out); },
          [&] {
              uint32_t before[9], mine[9];
              std::memcpy(before, out, sizeof before);
              std::memcpy(mine, out, sizeof mine);
              g_side = Side::Ours;
              DecalInfo(self, nullptr, id, mine);
              g_side = Side::Game;
              g_decalInfo(self, nullptr, id, out);
              g_side = Side::None;
              const bool same = std::memcmp(out, mine, sizeof mine) == 0;
              std::string a, b;
              for (int i = 0; i < 9; ++i) {
                  a += Hex32(out[i]);
                  b += Hex32(mine[i]);
              }
              compare::Record("renderer.overlays.manage1.decalinfo", same, a, b);
          });
}

void __fastcall DStats(char* self, void*, void* text)
{
    REACHED("GetStatsString");
    Route([&] { GetStatsString(self, nullptr, text); }, [&] { g_stats(self, nullptr, text); },
          [&] {
              const auto read = [&] { return std::string(Virtual<const char*>(text, 0x14)); };
              g_side = Side::Ours;
              GetStatsString(self, nullptr, text);
              const std::string mine = read();
              g_side = Side::Game;
              g_stats(self, nullptr, text);
              g_side = Side::None;
              const std::string result = read();
              compare::Record("renderer.overlays.manage1.stats", result == mine, result, mine);
          });
}

uint32_t __fastcall DQuery(char* self, void*, uint32_t iid, void** out)
{
    REACHED("QueryInterface");
    return Route([&] { return QueryInterface(self, nullptr, iid, out); }, [&] { return g_query(self, nullptr, iid, out); },
                 [&] {
                     void* mine = nullptr;
                     g_side = Side::Ours;
                     const uint32_t found = QueryInterface(self, nullptr, iid, &mine);
                     g_side = Side::Game;
                     const uint32_t result = g_query(self, nullptr, iid, out);
                     g_side = Side::None;
                     // Ours took a reference too.
                     if ((found & 0xFF) && mine)
                         Virtual<uint32_t>(mine, 0x8);
                     const bool same = (result & 0xFF) == (found & 0xFF) && (!(result & 0xFF) || *out == mine);
                     compare::Record("renderer.overlays.manage1.query", same, Hex32(result & 0xFF), Hex32(found & 0xFF));
                     return result;
                 });
}

uint32_t __fastcall DMessage(char* target, void*, void* message)
{
    REACHED("DoMessage");
    return Route([&] { return DoMessage(target, nullptr, message); }, [&] { return g_message(target, nullptr, message); },
                 [&] {
                     uint8_t before[12], mine[12];
                     std::memcpy(before, target + 8, sizeof before);
                     g_side = Side::Ours;
                     const uint32_t found = DoMessage(target, nullptr, message);
                     std::memcpy(mine, target + 8, sizeof mine);
                     std::memcpy(target + 8, before, sizeof before);
                     g_side = Side::Game;
                     const uint32_t result = g_message(target, nullptr, message);
                     g_side = Side::None;
                     const bool same = std::memcmp(mine, target + 8, 9) == 0 && (result & 0xFF) == (found & 0xFF);
                     uint32_t a[2], b[2];
                     std::memcpy(a, target + 8, sizeof a);
                     std::memcpy(b, mine, sizeof b);
                     compare::Record("renderer.overlays.manage1.message", same, Hex32(a[0]) + Hex32(a[1]), Hex32(b[0]) + Hex32(b[1]));
                     return result;
                 });
}

uint32_t __cdecl DMoved(const float* a, const float* b)
{
    REACHED("DecalMoved");
    return Route([&] { return Moved(a, b); }, [&] { return g_moved(a, b); },
                 [&] { return ComparePure("renderer.overlays.manage1.moved", [&] { return Moved(a, b); }, [&] { return g_moved(a, b); }); });
}

void __cdecl DCellRect(const float* rect, int32_t* out)
{
    REACHED("CellRect");
    Route([&] { CellRect(rect, out); }, [&] { g_cellRect(rect, out); },
          [&] {
              int32_t mine[4];
              g_side = Side::Ours;
              CellRect(rect, mine);
              g_side = Side::Game;
              g_cellRect(rect, out);
              g_side = Side::None;
              const bool same = std::memcmp(out, mine, sizeof mine) == 0;
              char a[64], b[64];
              std::snprintf(a, sizeof a, "%d %d %d %d", out[0], out[1], out[2], out[3]);
              std::snprintf(b, sizeof b, "%d %d %d %d", mine[0], mine[1], mine[2], mine[3]);
              compare::Record("renderer.overlays.manage1.cellrect", same, a, b);
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

bool InstallManage1(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    const Hooked hooks[] = {
        H<IdFn>(addr::cSC4OverlayManager_Invalidate, &DInvalidate, &g_invalidate),
        H<IdCellsFn>(addr::cSC4OverlayManager_BucketInsert, &DBucketInsert, &g_bucketInsert),
        H<IdCellsFn>(addr::cSC4OverlayManager_BucketRemove, &DBucketRemove, &g_bucketRemove),
        H<IdFn>(addr::cSC4OverlayManager_Place, &DPlace, &g_place),
        H<IdCellsFn>(addr::cSC4OverlayManager_SetCells, &DSetCells, &g_setCells),
        H<IdFn>(addr::cSC4OverlayManager_FreeHandle, &DFreeHandle, &g_freeHandle),
        H<IdFn>(addr::cSC4OverlayManager_RemoveOverlay, &DRemove, &g_remove),
        H<IdValueFn>(addr::cSC4OverlayManager_SetOverlayEnabled, &DSetEnabled, &g_setEnabled),
        H<IdFloatFn>(addr::cSC4OverlayManager_SetOverlayAlpha, &DSetAlpha, &g_setAlpha),
        H<IdPointerFn>(addr::cSC4OverlayManager_SetOverlayColor, &DSetColour, &g_setColour),
        H<IdValueFn>(addr::cSC4OverlayManager_SetOverlayDrawMode, &DSetDrawMode, &g_setDrawMode),
        H<IdValueFn>(addr::cSC4OverlayManager_SetOverlayFlags, &DSetFlags, &g_setFlags),
        H<HeightMapFn>(addr::cSC4OverlayManager_SetHeightMapParams, &DSetHeightMap, &g_setHeightMap),
        H<IdPointerFn>(addr::cSC4OverlayManager_MoveDecal, &DMoveDecal, &g_moveDecal),
        H<IdPointerFn>(addr::cSC4OverlayManager_UpdateDecalInfo, &DUpdateDecalInfo, &g_updateDecalInfo),
        H<GetFn>(addr::cSC4OverlayManager_OverlayIsEnabled, &DIsEnabled, &g_isEnabled),
        H<GetFloatFn>(addr::cSC4OverlayManager_OverlayAlpha, &DAlpha, &g_alpha),
        H<ColourFn>(addr::cSC4OverlayManager_OverlayColor, &DColour, &g_colour),
        H<GetFn>(addr::cSC4OverlayManager_OverlayDrawMode, &DDrawMode, &g_drawMode),
        H<GetFn>(addr::cSC4OverlayManager_OverlayFlags, &DFlags, &g_flags),
        H<InfoFn>(addr::cSC4OverlayManager_DecalInfo, &DDecalInfo, &g_decalInfo),
        H<TextFn>(addr::cSC4OverlayManager_GetStatsString, &DStats, &g_stats),
        H<QueryFn>(addr::cSC4OverlayManager_QueryInterface, &DQuery, &g_query),
        H<MessageFn>(addr::cSC4OverlayManager_DoMessage, &DMessage, &g_message),
        H<MovedFn>(addr::cSC4OverlayManager_DecalMoved, &DMoved, &g_moved),
        H<CellRectFn>(addr::cSC4OverlayManager_CellRect, &DCellRect, &g_cellRect),
    };
    for (const Hooked& h : hooks)
        if (!addr_check::Find(h.address)) {
            log::Write("overlays manage1: no address entry for 0x%08X", static_cast<unsigned>(h.address));
            return false;
        }
    g_standInVtable[kTerrainRedraw / 4] = reinterpret_cast<void*>(&StandInRedraw);
    g_compare = mode == registry::Mode::Compare;
    for (const Hooked& h : hooks)
        if (!hook::Install(*addr_check::Find(h.address), h.ours, h.original))
            return false;
    log::Write("overlays manage1: %u detours%s", static_cast<unsigned>(std::size(hooks)),
               g_compare ? ", compared with the game's" : "");
    return true;
}

const bool g_registered = registry::Register({
    "renderer.overlays.manage1",
    "renderer/overlays",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OverlayManager_RemoveOverlay,
    "The overlay manager's bookkeeping, part 1: the interface's getters and setters, removing, moving and updating "
    "overlays, the buckets and a few small functions (26 detours; compare mode runs ours on a copy of the manager and "
    "compares it with the game's; src/core/overlay_manage_1.cpp)",
    true,
    InstallManage1,
});

} // namespace
} // namespace opensc4::overlay_manage
