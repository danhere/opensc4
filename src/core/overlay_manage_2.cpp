// Our overlay manager's bookkeeping, part 2 (cSC4OverlayManager, build 641;
// docs/renderer.md, "The overlays", "Bookkeeping"): the adding. The
// interface's slots 3 to 11 (decals from a texture source, a texture, a key
// or an ID; ring decals; height maps; shadow decals; shadows), the new
// records they take from the free lists or the vectors' ends, the vectors'
// growth and the record copies under it, and the two decal builders: the
// texture matrix and cells of a decal from its description (0x0073A1B0) and
// the cells of a ring decal's eight overlays (0x007394B0).
//
// The layout is in overlay_manage_2.h; part 1 (overlay_manage_1.cpp) has the
// getters and setters, removing and the buckets. The detours and compare
// mode are in overlay_manage_2_hooks.cpp.
//
// The float code keeps the x87's evaluation: what the game holds on the
// register stack is a double here (the game runs at 53-bit precision), what
// it stores is a float, and the sums and products go in the game's order.
// fsin and fcos are the x87's own (their results are not rounded to the
// precision control).
//
// Calls the code makes: part 1's functions by address (CellRect 0x00735380,
// cdecl; BucketInsert, Invalidate, Place, SetCells, thiscall), so they reach
// ours or the game's as part 1 is set; the manager's own slots 4 and 9 and,
// for a group, nothing more; the texture manager's (+0x90) slots 0x14 (an
// ID), 0x18 (a key), 0x20 (a texture's size) and 0x2C (the group of its
// textures' keys); a texture source's slot 0xC (its size) and its AddRef and
// Release; the texture's AddRef 0x004664A0 and Release 0x008090E0; the
// terrain's (+0x88) slot 0x110; the lighting object 0x00B43DD0 (slot 0x24,
// then that object's 0x44, 0x48 and 0x34); SC4Rect::Intersect 0x0048ED90;
// the records' destructor over a range 0x00737E10 (part 3's); the allocator
// 0x0090CF54 and its free 0x0090CF63.
#include "core/overlay_manage_2.h"

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstring>

namespace opensc4::overlay_manage::part2 {
namespace {

// The constants the code reads (0x00A81054, 0x00A81228, 0x00A84D2C,
// 0x00AB390C, 0x00AB3914, 0x00A80AB0, 0x00A80AA8, 0x00A92D28, 0x00A8821C,
// 0x00A8825C, 0x00AA74E8, 0x00AA27E8, 0x00A8E174).
constexpr float kZero = 0.0f, kOne = 1.0f, kHalf = 0.5f, kQuarter = 0.25f, kTwo = 2.0f;
constexpr float kRoundBias = 12582912.0f; // 1.5 * 2^23: a float this size has no fraction bits
constexpr float kTurn = 6.28318548f;      // 2 pi as a float
constexpr double kOneDouble = 1.0, kHalfDouble = 0.5, kQuarterDouble = 0.25;
constexpr double kTwoTo32 = 4294967296.0;
constexpr float kShadowMargin = 0.07f;  // 0x3D8F5C29
constexpr float kShadowHeight = 0.0625f; // a shadow's reach per unit of height
constexpr uint32_t kUnitBits = 0x3F800000;
constexpr uint32_t kFshType = 0x7AB50E44;

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

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The x87's FILD of an unsigned word: the signed value, 2^32 added when it
// is negative.
double Unsigned(uint32_t value)
{
    const double d = static_cast<int32_t>(value);
    return static_cast<int32_t>(value) < 0 ? d + kTwoTo32 : d;
}

// The rounding of the game's cell code: v + 0.5 (or v - 0.5) and 1.5 * 2^23
// stored as a float, its bits less the bias's, is the integer nearest; the
// upper end is one less when v is already that integer less one, the lower
// one more when v is that integer plus one.
int32_t RoundedBits(double sum)
{
    return static_cast<int32_t>(Bits(static_cast<float>(sum)) - 0x4B400000u);
}

int32_t RoundUp(double v)
{
    const int32_t n = RoundedBits((static_cast<double>(kHalf) + v) + kRoundBias);
    return v == static_cast<double>(n - 1) ? n - 1 : n;
}

int32_t RoundDown(double v)
{
    const int32_t n = RoundedBits((v - kHalf) + kRoundBias);
    return v == static_cast<double>(n + 1) ? n + 1 : n;
}

// fcos and fsin of an angle, each stored as a float.
void X87CosSin(double angle, float& cosine, float& sine)
{
    float c, s;
    __asm {
        fld angle
        fld st(0)
        fcos
        fstp c
        fsin
        fstp s
    }
    cosine = c;
    sine = s;
}

char* Overlay(char* self, uint32_t index)
{
    return At<char*>(self, kOverlays) + index * kOverlaySize;
}

void TextureAddRef(void* texture)
{
    This<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
}

void TextureRelease(void* texture)
{
    This<uint32_t>(addr::SC4TextureBinding_Release, texture);
}

// A record's texture (+0xAC) or texture source (+0xB0) set, the new one
// AddRef'd and the old one released as the game's smart pointers do.
void SetTexture(char* record, void* texture)
{
    void* const old = At<void*>(record, kTexture);
    if (texture == old)
        return;
    if (texture)
        TextureAddRef(texture);
    At<void*>(record, kTexture) = texture;
    if (old)
        TextureRelease(old);
}

void SetTextureSource(char* record, void* source)
{
    void* const old = At<void*>(record, kTextureSource);
    if (source == old)
        return;
    if (source)
        Virtual<uint32_t>(source, 0x4);
    At<void*>(record, kTextureSource) = source;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// What a decal's local record holds at the end: its source and texture
// released.
void ReleaseRecord(char* record)
{
    if (void* const source = At<void*>(record, kTextureSource))
        Virtual<uint32_t>(source, 0x8);
    if (void* const texture = At<void*>(record, kTexture))
        TextureRelease(texture);
}

// The size of a texture from the texture manager (slot 0x20, seven words:
// width +0xC, height +0x10), height over width.
float TextureAspect(char* self, void* texture)
{
    uint32_t info[8] = {};
    Virtual<void>(At<void*>(self, kTextures), 0x20, texture, static_cast<void*>(info));
    return static_cast<float>(Unsigned(info[4]) / Unsigned(info[3]));
}

// A decal's handle as the adds start it: the position, size and turn, unit
// aspect and texture scales, no offset.
void StartHandle(uint32_t* handle, const float* position, float size, float turn)
{
    std::memcpy(&handle[1], position, 2 * sizeof(float));
    handle[3] = Bits(size);
    handle[4] = Bits(turn);
    handle[5] = kUnitBits;
    handle[6] = kUnitBits;
    handle[7] = kUnitBits;
    handle[8] = 0;
    handle[9] = 0;
}

void BucketInsert(char* self, uint32_t index)
{
    This<void>(addr::cSC4OverlayManager_BucketInsert, self, index, &At<int32_t>(Overlay(self, index), kCells));
}

void Invalidate(char* self, uint32_t index)
{
    This<void>(addr::cSC4OverlayManager_Invalidate, self, index);
}

void SetCells(char* self, uint32_t index, const int32_t* cells)
{
    This<void>(addr::cSC4OverlayManager_SetCells, self, index, cells);
}

// SC4Rect::Intersect into `out` (always written; zeros when they miss).
void Intersect(int32_t* out, const int32_t* a, const int32_t* b)
{
    This<uint8_t>(addr::SC4Rect_Intersect, out, a, b);
}

// The cells of a decal or a ring decal's piece: through SetCells (out of
// its buckets and into the new ones) or, for a new overlay, cut to the
// terrain in place.
void PutCells(char* self, uint32_t index, const int32_t* cells, uint32_t buckets)
{
    if (buckets & 0xFF)
        SetCells(self, index, cells);
    else
        Intersect(&At<int32_t>(Overlay(self, index), kCells), &At<int32_t>(self, kClip), cells);
}

} // namespace

// ====================================================================================
// The records
// ====================================================================================

// 0x00735F40: identity first matrix, the second unset, alpha 1, the default
// colour (0x00B0D4E4), no texture, flag 1. The index, the byte at +9 and the
// cells are left as they were.
char* __fastcall ResetOverlay(char* record)
{
    At<uint32_t>(record, kFlags) = 0;
    At<uint8_t>(record, kDrawMode) = 0;
    At<uint16_t>(record, kStamp) = 0;
    float* const m = &At<float>(record, kMatrix);
    for (int i = 0; i < 16; ++i)
        m[i] = i % 5 == 0 ? kOne : kZero;
    const uint32_t unset = Global<uint32_t>(addr::STE_UnsetMatrixFloat);
    uint32_t* const second = &At<uint32_t>(record, kSecondMatrix);
    for (int i = 0; i < 16; ++i)
        second[i] = unset;
    At<float>(record, kAlpha) = kOne;
    std::memcpy(&At<uint32_t>(record, kColour), &Global<uint32_t>(addr::STE_OverlayColour), 3 * sizeof(uint32_t));
    At<void*>(record, kTexture) = nullptr;
    At<void*>(record, kTextureSource) = nullptr;
    At<uint32_t>(record, kFlags) |= 1;
    return record;
}

// 0x007359F0: a copy made, its texture and source AddRef'd. The byte at +9 is
// not copied.
char* __fastcall CopyOverlay(char* to, void*, const char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(const_cast<char*>(from), 0);
    At<uint32_t>(to, kFlags) = At<uint32_t>(const_cast<char*>(from), kFlags);
    At<uint8_t>(to, kDrawMode) = At<uint8_t>(const_cast<char*>(from), kDrawMode);
    At<uint16_t>(to, kStamp) = At<uint16_t>(const_cast<char*>(from), kStamp);
    std::memcpy(to + kCells, from + kCells, kTexture - kCells);
    void* const texture = At<void*>(const_cast<char*>(from), kTexture);
    At<void*>(to, kTexture) = texture;
    if (texture)
        TextureAddRef(texture);
    void* const source = At<void*>(const_cast<char*>(from), kTextureSource);
    At<void*>(to, kTextureSource) = source;
    if (source)
        Virtual<uint32_t>(source, 0x4);
    return to;
}

// 0x00735820: assigned, the texture and source swapped by reference. The byte
// at +9 is kept.
char* __fastcall AssignOverlay(char* to, void*, const char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(const_cast<char*>(from), 0);
    At<uint32_t>(to, kFlags) = At<uint32_t>(const_cast<char*>(from), kFlags);
    At<uint8_t>(to, kDrawMode) = At<uint8_t>(const_cast<char*>(from), kDrawMode);
    At<uint16_t>(to, kStamp) = At<uint16_t>(const_cast<char*>(from), kStamp);
    std::memcpy(to + kCells, from + kCells, kTexture - kCells);
    if (from != to) {
        SetTexture(to, At<void*>(const_cast<char*>(from), kTexture));
        SetTextureSource(to, At<void*>(const_cast<char*>(from), kTextureSource));
    }
    return to;
}

char* __cdecl AssignOverlays(const char* first, const char* last, char* to)
{
    for (int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(kOverlaySize); count > 0; --count) {
        AssignOverlay(to, nullptr, first);
        first += kOverlaySize;
        to += kOverlaySize;
    }
    return to;
}

char* __cdecl AssignHandles(const char* first, const char* last, char* to)
{
    for (int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(kHandleSize); count > 0; --count) {
        std::memcpy(to, first, kHandleSize);
        first += kHandleSize;
        to += kHandleSize;
    }
    return to;
}

char* __cdecl CopyHandle(char* to, const char* from)
{
    if (to)
        std::memcpy(to, from, kHandleSize);
    return to;
}

char* __cdecl CopyOverlays(const char* first, const char* last, char* to)
{
    for (; first != last; first += kOverlaySize, to += kOverlaySize)
        if (to)
            CopyOverlay(to, nullptr, first);
    return to;
}

char* __cdecl FillOverlays(char* to, uint32_t count, const char* from)
{
    for (; count; --count, to += kOverlaySize)
        if (to)
            CopyOverlay(to, nullptr, from);
    return to;
}

char* __cdecl CopyHandles(const char* first, const char* last, char* to)
{
    for (; first != last; first += kHandleSize, to += kHandleSize)
        if (to)
            std::memcpy(to, first, kHandleSize);
    return to;
}

char* __cdecl FillHandles(char* to, uint32_t count, const char* from)
{
    for (; count; --count, to += kHandleSize)
        if (to)
            std::memcpy(to, from, kHandleSize);
    return to;
}

uint32_t* __cdecl CopyGroups(const uint32_t* first, const uint32_t* last, uint32_t* to)
{
    for (; first != last; first += kGroupSize / 4, to += kGroupSize / 4)
        if (to)
            std::memcpy(to, first, kGroupSize);
    return to;
}

// ====================================================================================
// The vectors
// ====================================================================================

namespace {

// STLport's insert overflow: room for max(size, n) more, the records before
// `at` copied over, n copies of `from`, the ones after unless inserting at the
// end; the old records gone and the old block freed.
template <typename CopyRange, typename CopyOne, typename Fill, typename Destroy>
void Grow(char* vector, std::size_t size, char* at, const char* from, uint32_t count, uint32_t atEnd, CopyRange copyRange,
          CopyOne copyOne, Fill fill, Destroy destroy)
{
    char*& begin = At<char*>(vector, 0);
    char*& end = At<char*>(vector, kEnd);
    const uint32_t used = static_cast<uint32_t>(static_cast<int32_t>(end - begin) / static_cast<int32_t>(size));
    const uint32_t capacity = (count > used ? count : used) + used;
    char* const block = capacity ? Cdecl<char*>(addr::SC4_stl_allocate, static_cast<std::size_t>(capacity * size)) : nullptr;
    char* p = copyRange(begin, at, block);
    if (count == 1) {
        copyOne(p, from);
        p += size;
    } else {
        p = fill(p, count, from);
    }
    if (!(atEnd & 0xFF))
        p = copyRange(at, end, p);
    destroy(begin, end);
    if (begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
    end = p;
    begin = block;
    At<char*>(vector, kCapacity) = block + capacity * size;
}

} // namespace

// 0x0073A070.
void __fastcall GrowOverlays(char* vector, void*, char* at, const char* from, const void*, uint32_t count, uint32_t atEnd)
{
    Grow(
        vector, kOverlaySize, at, from, count, atEnd, [](const char* f, const char* l, char* t) { return CopyOverlays(f, l, t); },
        [](char* t, const char* f) {
            if (t)
                CopyOverlay(t, nullptr, f);
        },
        [](char* t, uint32_t n, const char* f) { return FillOverlays(t, n, f); },
        [](char* f, char* l) {
            char tag = 0;
            Cdecl<void>(addr::cSC4OverlayManager_DestroyOverlays, f, l, static_cast<void*>(&tag));
        });
}

// 0x007385C0.
void __fastcall GrowHandles(char* vector, void*, char* at, const char* from, const void*, uint32_t count, uint32_t atEnd)
{
    Grow(
        vector, kHandleSize, at, from, count, atEnd, [](const char* f, const char* l, char* t) { return CopyHandles(f, l, t); },
        [](char* t, const char* f) { CopyHandle(t, f); }, [](char* t, uint32_t n, const char* f) { return FillHandles(t, n, f); },
        [](char*, char*) {});
}

// 0x007386A0 (the copies inlined in the game's, its tail through 0x00735350).
void __fastcall GrowGroups(char* vector, void*, char* at, const char* from, const void*, uint32_t count, uint32_t atEnd)
{
    const auto copyRange = [](const char* f, const char* l, char* t) {
        return reinterpret_cast<char*>(
            CopyGroups(reinterpret_cast<const uint32_t*>(f), reinterpret_cast<const uint32_t*>(l), reinterpret_cast<uint32_t*>(t)));
    };
    const auto copyOne = [](char* t, const char* f) {
        if (t)
            std::memcpy(t, f, kGroupSize);
    };
    const auto fill = [](char* t, uint32_t n, const char* f) {
        for (; n; --n, t += kGroupSize)
            if (t)
                std::memcpy(t, f, kGroupSize);
        return t;
    };
    Grow(vector, kGroupSize, at, from, count, atEnd, copyRange, copyOne, fill, [](char*, char*) {});
}

namespace {

// A new record's index: the first free one (the list moved on, or emptied
// at its last) assigned, or a new one at the end (the vector grown when
// full); its link set to -1.
template <typename Assign, typename Push, typename GrowFn>
uint32_t NewRecord(char* vector, std::size_t size, const char* from, Assign assign, Push push, GrowFn grow)
{
    char* const begin = At<char*>(vector, 0);
    const uint32_t free = At<uint32_t>(vector, kFirstFree);
    if (free == kNone) {
        char*& end = At<char*>(vector, kEnd);
        if (end == At<char*>(vector, kCapacity)) {
            grow(vector, end, from);
        } else {
            push(end, from);
            end += size;
        }
        At<uint32_t>(At<char*>(vector, kEnd) - size, 0) = kNone;
        return static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, kEnd) - At<char*>(vector, 0)) / static_cast<int32_t>(size))
               - 1;
    }
    if (free == At<uint32_t>(vector, kLastFree)) {
        At<uint32_t>(vector, kFirstFree) = kNone;
        At<uint32_t>(vector, kLastFree) = kNone;
    } else {
        At<uint32_t>(vector, kFirstFree) = At<uint32_t>(begin + free * size, 0);
    }
    assign(begin + free * size, from);
    At<uint32_t>(At<char*>(vector, 0) + free * size, 0) = kNone;
    return free;
}

} // namespace

// 0x0073AEC0.
uint32_t __fastcall NewOverlay(char* vector, void*, const char* from)
{
    return NewRecord(
        vector, kOverlaySize, from, [](char* t, const char* f) { AssignOverlay(t, nullptr, f); },
        [](char* t, const char* f) {
            if (t)
                CopyOverlay(t, nullptr, f);
        },
        [](char* v, char* end, const char* f) {
            const char* tag = f;
            GrowOverlays(v, nullptr, end, f, &tag, 1, 1);
        });
}

// 0x00739EC0.
uint32_t __fastcall NewHandle(char* vector, void*, const char* from)
{
    return NewRecord(
        vector, kHandleSize, from, [](char* t, const char* f) { std::memcpy(t, f, kHandleSize); },
        [](char* t, const char* f) { CopyHandle(t, f); },
        [](char* v, char* end, const char* f) {
            const char* tag = f;
            GrowHandles(v, nullptr, end, f, &tag, 1, 1);
        });
}

// 0x00739FC0.
uint32_t __fastcall NewGroup(char* vector, void*, const char* from)
{
    return NewRecord(
        vector, kGroupSize, from, [](char* t, const char* f) { std::memcpy(t, f, kGroupSize); },
        [](char* t, const char* f) {
            if (t)
                std::memcpy(t, f, kGroupSize);
        },
        [](char* v, char* end, const char* f) {
            const char* tag = f;
            GrowGroups(v, nullptr, end, f, &tag, 1, 1);
        });
}

// ====================================================================================
// The decal builders
// ====================================================================================

// 0x0073A1B0, ret 8 (handle, bool buckets): BuildDecal. From the handle's
// description (centre +4, size +0xC, turn +0x10, aspect +0x14, texture scales
// +0x18 and +0x1C, offset +0x20, and the texture's aspect +0x28) the texture
// matrix that maps the decal's ground square to its texture, given to the
// overlay, or to each of a group's eight with the ring's cells built after
// (0x007394B0); else the overlay's cells from the decal's bounds (0x00735380):
// through SetCells with buckets, or cut to the terrain in place. Flag 2 is
// set unless both texture scales are 1. A turned decal's bounds grow to hold
// it when the texture scales are 1.
void __fastcall BuildDecal(char* self, void*, char* handle, uint32_t buckets)
{
    const float cx = At<float>(handle, 0x4), cz = At<float>(handle, 0x8), size = At<float>(handle, 0xC);
    const float turn = At<float>(handle, 0x10), scaleU = At<float>(handle, 0x18), scaleV = At<float>(handle, 0x1C);
    const float offset = At<float>(handle, 0x20);
    const bool unitScales = At<uint32_t>(handle, 0x18) == kUnitBits && At<uint32_t>(handle, 0x1C) == kUnitBits;

    // The decal's width and depth and the matrix as for no turn (m[row * 4 +
    // column]; m[8] and m[9] are 0).
    const float width = static_cast<float>(static_cast<double>(At<float>(handle, 0x14)) * At<float>(handle, kAspect));
    const double inverseSize = kOneDouble / size;
    float halfX = static_cast<float>(static_cast<double>(size) * kHalf);
    float halfZ = static_cast<float>(static_cast<double>(width) * size * kHalf);
    const double fromX = static_cast<double>(halfX) - cx;
    const float zeroX = static_cast<float>(inverseSize * kZero);
    const float shiftX = static_cast<float>(fromX * inverseSize);
    const double scaleX = inverseSize * scaleU;
    const float m0 = static_cast<float>(scaleX);
    const float m4 = static_cast<float>(static_cast<double>(scaleU) * zeroX);
    const float m8 = static_cast<float>(static_cast<double>(scaleU) * zeroX);
    const float shiftU = static_cast<float>(static_cast<double>(scaleU) * shiftX);
    const float fromZ = static_cast<float>(static_cast<double>(halfZ) - cz);
    const double inverseWidth = kOneDouble / width;
    const float inverseWidthF = static_cast<float>(inverseWidth);
    const double scaleZ = inverseSize * inverseWidth;
    const double zeroZ = scaleZ * kZero;
    const float zeroZF = static_cast<float>(zeroZ);
    const float scaleZF = static_cast<float>(scaleZ);
    const float shiftZ = static_cast<float>(scaleZ * fromZ);
    const float m1 = static_cast<float>(static_cast<double>(zeroZF) * scaleV);
    const double m5Exact = static_cast<double>(scaleV) * zeroZ;
    const float m5 = static_cast<float>(m5Exact);
    const float m9 = static_cast<float>(static_cast<double>(scaleV) * scaleZF);
    const float shiftV = static_cast<float>(static_cast<double>(scaleV) * shiftZ);
    const double row3U = static_cast<double>(shiftU) + offset;
    const float row3V = static_cast<float>(static_cast<double>(shiftV) + offset);

    // The matrix as stored: rows of four (m[12] is the stack value below).
    float m[16] = {m0, m1, kZero, kZero, m4, m5, kZero, kZero, m8, m9, kOne, kZero, 0.0f, row3V, kZero, kOne};
    double m12 = row3U;

    if (!(turn == kZero)) {
        // Turned about the decal's centre: the matrix times a turn by the
        // angle, with the shift that keeps the centre.
        float c, s;
        X87CosSin(static_cast<double>(turn) * kTurn, c, s);
        const float negS = static_cast<float>(-static_cast<double>(s));
        const double k = kOneDouble - static_cast<double>(c);
        const float l = static_cast<float>(static_cast<double>(cz) * s + static_cast<double>(cx) * k);
        const double f = static_cast<double>(cz) * k - static_cast<double>(cx) * s;
        const float zero4 = static_cast<float>(static_cast<double>(m4) * kZero);
        const float u = static_cast<float>(((static_cast<double>(m8) * f + static_cast<double>(m0) * l) + zero4) + row3U);
        const float zero5 = static_cast<float>(static_cast<double>(m5) * kZero);
        const float v = static_cast<float>(((static_cast<double>(m9) * f + static_cast<double>(m1) * l) + zero5) + row3V);
        const double lZero = static_cast<double>(l) * kZero;
        const float m14 = static_cast<float>(lZero + f);
        const float m15 = static_cast<float>((f * kZero + lZero) + kOne);
        const float zeroU = static_cast<float>(row3U * kZero);
        const double x1 = ((static_cast<double>(m8) * c + static_cast<double>(m0) * negS) + zeroU) + zero4;
        const float zeroV = static_cast<float>(static_cast<double>(row3V) * kZero);
        const double x2 = ((static_cast<double>(m9) * c + static_cast<double>(m1) * negS) + zeroV) + zero5;
        const double negSZero = static_cast<double>(negS) * kZero;
        const float m10 = static_cast<float>(static_cast<double>(c) + negSZero);
        const double cZero = static_cast<double>(c) * kZero;
        const float cZeroF = static_cast<float>(cZero);
        const float m11 = static_cast<float>(cZero + negSZero);
        const double x3 = ((static_cast<double>(m0) + m8) * kZero + zeroU) + m4;
        const double x4 = ((static_cast<double>(m1) + m9) * kZero + zeroV) + m5;
        const double x5 = ((static_cast<double>(m0) * c + static_cast<double>(m8) * s) + zeroU) + zero4;
        const double x6 = ((static_cast<double>(m1) * c + static_cast<double>(m9) * s) + zeroV) + zero5;
        const float m2 = static_cast<float>(static_cast<double>(cZeroF) + s);
        const float m3 = static_cast<float>(static_cast<double>(s) * kZero + cZeroF);
        m[0] = static_cast<float>(x5);
        m[1] = static_cast<float>(x6);
        m[2] = m2;
        m[3] = m3;
        m[4] = static_cast<float>(x3);
        m[5] = static_cast<float>(x4);
        m[8] = static_cast<float>(x1);
        m[9] = static_cast<float>(x2);
        m[10] = m10;
        m[11] = m11;
        m[13] = v;
        m[14] = m14;
        m[15] = m15;
        m12 = u;
        if (unitScales) {
            const double absC = std::fabs(static_cast<double>(c)), absS = std::fabs(static_cast<double>(s));
            halfX = static_cast<float>((static_cast<double>(width) * absS + absC) * halfX);
            halfZ = static_cast<float>((static_cast<double>(inverseWidthF) * absS + absC) * halfZ);
        }
    }

    const double cellScale = At<float>(self, kCellScale);
    const float rect[4] = {static_cast<float>((static_cast<double>(cx) - halfX) * cellScale),
                           static_cast<float>((static_cast<double>(cz) - halfZ) * cellScale),
                           static_cast<float>((static_cast<double>(halfX) + cx) * cellScale),
                           static_cast<float>((static_cast<double>(halfZ) + cz) * cellScale)};

    const auto store = [&](char* overlay) {
        float* const out = &At<float>(overlay, kMatrix);
        out[0] = m[0];
        out[1] = m[1];
        out[2] = m[2];
        out[3] = m[3];
        out[4] = m[4];
        out[5] = m[5];
        out[6] = kZero;
        out[7] = kZero;
        out[8] = m[8];
        out[9] = m[9];
        out[10] = m[10];
        out[11] = m[11];
        out[13] = m[13];
        out[12] = static_cast<float>(m12);
        out[14] = m[14];
        out[15] = m[15];
        uint32_t& flags = At<uint32_t>(overlay, kFlags);
        flags = unitScales ? flags & ~2u : flags | 2u;
    };

    const uint32_t target = At<uint32_t>(handle, kTarget);
    if (static_cast<int32_t>(target) < 0) {
        const uint32_t* const group =
            reinterpret_cast<const uint32_t*>(At<char*>(self, kGroups) + (target & kIndexMask) * kGroupSize);
        for (int i = 0; i < kGroupCount; ++i)
            store(Overlay(self, group[1 + i]));
        BuildRing(self, nullptr, group, rect, buckets);
        return;
    }
    store(Overlay(self, target));
    int32_t cells[4];
    Cdecl<void>(addr::cSC4OverlayManager_CellRect, rect, cells);
    PutCells(self, At<uint32_t>(handle, kTarget), cells, buckets);
}

// 0x007394B0, ret 0xC (group, float[4] bounds in cells, bool buckets): the
// cells of a ring decal's eight overlays. The ring's outer radius is half the
// bounds' width, its inner one (1 - 0x00B0D4D8) of that. Two rows are taken
// in from the top and the bottom (0x00B0D4D4 and 0x00B0D4D0 of half the
// depth), and the ring's half-widths at those rows give the pieces: a band
// across the top and one across the bottom, the middle rows' left and right
// sides, and four pieces between.
void __fastcall BuildRing(char* self, void*, const uint32_t* group, const float* rect, uint32_t buckets)
{
    const double width = static_cast<double>(rect[2]) - rect[0];
    const float depth = static_cast<float>(static_cast<double>(rect[3]) - rect[1]);
    const float cx = static_cast<float>((static_cast<double>(rect[0]) + rect[2]) * kHalf);
    const float cz = static_cast<float>((static_cast<double>(rect[1]) + rect[3]) * kHalf);
    const double outer = kHalfDouble * width;
    const float outerSquared = static_cast<float>(outer * outer);
    const double inner = ((static_cast<double>(kOne) - Global<float>(addr::STE_RingThickness)) * width) * kHalfDouble;
    const float innerSquared = static_cast<float>(inner * inner);
    int32_t cells[4];
    Cdecl<void>(addr::cSC4OverlayManager_CellRect, rect, cells);

    const int32_t near = RoundUp((static_cast<double>(Global<float>(addr::STE_RingRowNear)) * depth) * kHalfDouble);
    const int32_t far = RoundUp((static_cast<double>(Global<float>(addr::STE_RingRowFar)) * depth) * kHalfDouble);
    const int32_t z1 = cells[1] + near, z2 = cells[1] + far, z3 = cells[3] - far, z4 = cells[3] - near;

    // A circle's half-width at a row (0 where the row misses it): the square
    // of the row's distance is compared before it is rounded and used
    // rounded.
    const auto halfWidth = [&](int32_t row, float radiusSquared) {
        const double d = static_cast<double>(cz) - row;
        const double squared = d * d;
        const float rounded = static_cast<float>(squared);
        return std::sqrt(static_cast<double>(radiusSquared) - (squared < radiusSquared ? rounded : radiusSquared));
    };
    const float outer1 = static_cast<float>(halfWidth(z1, outerSquared)), inner1 = static_cast<float>(halfWidth(z1, innerSquared));
    const float outer2 = static_cast<float>(halfWidth(z2, outerSquared)), inner2 = static_cast<float>(halfWidth(z2, innerSquared));
    const float outer3 = static_cast<float>(halfWidth(z3, outerSquared)), inner3 = static_cast<float>(halfWidth(z3, innerSquared));
    const float outer4 = static_cast<float>(halfWidth(z4, outerSquared));
    const double inner4 = halfWidth(z4, innerSquared); // kept unrounded

    const double x = cx;
    const int32_t x1 = RoundDown(x - outer1), x2 = RoundUp(x - inner1), x3 = RoundDown(inner1 + x), x4 = RoundUp(outer1 + x);
    const int32_t x5 = RoundDown(x - outer2), x6 = RoundUp(x - inner2), x7 = RoundDown(inner2 + x), x8 = RoundUp(outer2 + x);
    const int32_t x9 = RoundDown(x - outer3), x10 = RoundUp(x - inner3), x11 = RoundDown(inner3 + x), x12 = RoundUp(outer3 + x);
    const int32_t x13 = RoundDown(x - outer4), x14 = RoundUp(x - inner4), x15 = RoundDown(inner4 + x), x16 = RoundUp(outer4 + x);

    // The middle rows' left piece ends where the inner circle starts on the
    // later of its two rows, no further than where the right piece starts.
    int32_t leftEnd = x6 < x10 ? x10 : x6;
    const int32_t rightStart = x11 < x7 ? x11 : x7;
    if (leftEnd > rightStart)
        leftEnd = rightStart;

    const int32_t pieces[kGroupCount][4] = {
        {x1, cells[1], x4, z1},         // the top band
        {x13, z4, x16, cells[3]},       // the bottom band
        {cells[0], z2, leftEnd, z3},    // the middle rows, left
        {rightStart, z2, cells[2], z3}, // the middle rows, right
        {x5, z1, x2, z2},               // below the top band, left
        {x9, z3, x14, z4},              // above the bottom band, left
        {x3, z1, x8, z2},               // below the top band, right
        {x15, z3, x12, z4},             // above the bottom band, right
    };
    for (int i = 0; i < kGroupCount; ++i)
        PutCells(self, group[1 + i], pieces[i], buckets);
}

// 0x007354E0, ret 0x14 (light direction, count, points, float[6] bounds, out
// int32[4]): the cells under a shadow: the bounds' ground rectangle, 0.07 in
// from each side, in cells, stretched away from the light on each axis by its
// height (a cell for 16), cut to the terrain.
void __fastcall ShadowCells(char* self, void*, const float* light, uint32_t, const void*, const float* box, int32_t* out)
{
    const double scale = At<float>(self, kCellScale);
    int32_t x0 = RoundDown((static_cast<double>(box[0]) + kShadowMargin) * scale);
    int32_t x1 = RoundUp((static_cast<double>(box[3]) - kShadowMargin) * scale);
    int32_t z0 = RoundDown((static_cast<double>(box[2]) + kShadowMargin) * scale);
    int32_t z1 = RoundUp((static_cast<double>(box[5]) - kShadowMargin) * scale);
    const int32_t reach = RoundUp((static_cast<double>(box[4]) - box[1]) * kShadowHeight);
    if (light[0] > kZero)
        x1 += reach;
    else
        x0 -= reach;
    if (light[2] > kZero)
        z1 += reach;
    else
        z0 -= reach;
    int32_t cells[4] = {x0, z0, x1, z1};
    Intersect(cells, &At<int32_t>(self, kClip), cells);
    std::memcpy(out, cells, sizeof cells);
}

// ====================================================================================
// The adds
// ====================================================================================

// 0x0073AF80, ret 0x14 (texture source, texture, position, size, turn): a
// decal. Its aspect is the texture's (from the texture manager), else the
// source's (its slot 0xC); the overlay (flags 0x90) is built without
// buckets, then put into its buckets and redrawn, and a handle is made for
// it. -1 with neither.
uint32_t __fastcall AddDecalFrom(char* self, void*, void* source, void* texture, const float* position, float size, float turn)
{
    if (!texture && !source)
        return kNone;
    uint32_t handle[kHandleSize / 4] = {};
    StartHandle(handle, position, size, turn);
    float aspect;
    if (texture) {
        aspect = TextureAspect(self, texture);
    } else {
        uint32_t extent[2] = {};
        Virtual<void>(source, 0xC, static_cast<void*>(extent));
        aspect = static_cast<float>(Unsigned(extent[1]) / Unsigned(extent[0]));
    }
    handle[kAspect / 4] = Bits(aspect);
    alignas(4) char record[kOverlaySize] = {};
    ResetOverlay(record);
    SetTexture(record, texture);
    SetTextureSource(record, source);
    At<uint32_t>(record, kFlags) |= 0x90;
    handle[kTarget / 4] = NewOverlay(self + kOverlays, nullptr, record);
    BuildDecal(self, nullptr, reinterpret_cast<char*>(handle), 0);
    const uint32_t index = handle[kTarget / 4];
    if (index != kNone && !At<uint8_t>(self, kWaterRows)) {
        BucketInsert(self, index);
        Invalidate(self, index);
    }
    const uint32_t id = NewHandle(self + kHandles, nullptr, reinterpret_cast<const char*>(handle));
    ReleaseRecord(record);
    return id | kHandleBit;
}

// Slot 3, 0x0073C0B0, ret 0x10: a decal from a texture source.
uint32_t __fastcall AddDecalSource(char* self, void*, void* source, const float* position, float size, float turn)
{
    return AddDecalFrom(self, nullptr, source, nullptr, position, size, turn);
}

// Slot 4, 0x0073BF10, ret 0x10: a decal from a texture (-1 for none), as
// 0x0073AF80 does it.
uint32_t __fastcall AddDecalTexture(char* self, void*, void* texture, const float* position, float size, float turn)
{
    if (!texture)
        return kNone;
    uint32_t handle[kHandleSize / 4] = {};
    StartHandle(handle, position, size, turn);
    handle[kAspect / 4] = Bits(TextureAspect(self, texture));
    alignas(4) char record[kOverlaySize] = {};
    ResetOverlay(record);
    if (texture != At<void*>(record, kTexture)) {
        void* const old = At<void*>(record, kTexture);
        TextureAddRef(texture);
        At<void*>(record, kTexture) = texture;
        if (old)
            TextureRelease(old);
    }
    if (void* const source = At<void*>(record, kTextureSource)) {
        At<void*>(record, kTextureSource) = nullptr;
        Virtual<uint32_t>(source, 0x8);
    }
    At<uint32_t>(record, kFlags) |= 0x90;
    handle[kTarget / 4] = NewOverlay(self + kOverlays, nullptr, record);
    BuildDecal(self, nullptr, reinterpret_cast<char*>(handle), 0);
    const uint32_t index = handle[kTarget / 4];
    if (index != kNone && !At<uint8_t>(self, kWaterRows)) {
        BucketInsert(self, index);
        Invalidate(self, index);
    }
    const uint32_t id = NewHandle(self + kHandles, nullptr, reinterpret_cast<const char*>(handle));
    ReleaseRecord(record);
    return id | kHandleBit;
}

// Slots 5 and 6, 0x00736150 and 0x007361B0, ret 0x10: a decal from the
// texture the texture manager finds by key (its slot 0x18) or by ID (0x14),
// through slot 4; the texture released after.
uint32_t __fastcall AddDecalKey(char* self, void*, const uint32_t* key, const float* position, float size, float turn)
{
    void* texture = nullptr;
    Virtual<void>(At<void*>(self, kTextures), 0x18, key, static_cast<void*>(&texture));
    const uint32_t id = Virtual<uint32_t>(self, 0x10, texture, position, size, turn);
    if (texture)
        TextureRelease(texture);
    return id;
}

uint32_t __fastcall AddDecalIid(char* self, void*, uint32_t iid, const float* position, float size, float turn)
{
    void* texture = nullptr;
    Virtual<void>(At<void*>(self, kTextures), 0x14, iid, static_cast<void*>(&texture));
    const uint32_t id = Virtual<uint32_t>(self, 0x10, texture, position, size, turn);
    if (texture)
        TextureRelease(texture);
    return id;
}

// Slot 7, 0x0073B170, ret 0x10 (texture ID, position, size, turn): a ring
// decal: eight overlays (flags 0x10) with the texture, a group of them, the
// decal built on the group without buckets, each overlay put into its
// buckets and its cells redrawn (the terrain's slot 0x110 called inline),
// and a handle for the group. -1 when the texture manager has no such
// texture.
uint32_t __fastcall AddRingDecal(char* self, void*, uint32_t iid, const float* position, float size, float turn)
{
    void* texture = nullptr;
    Virtual<void>(At<void*>(self, kTextures), 0x14, iid, static_cast<void*>(&texture));
    if (!texture)
        return kNone;
    uint32_t handle[kHandleSize / 4] = {};
    StartHandle(handle, position, size, turn);
    handle[kAspect / 4] = Bits(TextureAspect(self, texture));
    uint32_t group[kGroupSize / 4] = {};
    alignas(4) char record[kOverlaySize] = {};
    for (int i = 0; i < kGroupCount; ++i) {
        ResetOverlay(record);
        SetTexture(record, texture);
        At<uint32_t>(record, kFlags) |= 0x10;
        group[1 + i] = NewOverlay(self + kOverlays, nullptr, record);
        ReleaseRecord(record);
    }
    handle[kTarget / 4] = NewGroup(self + kGroups, nullptr, reinterpret_cast<const char*>(group)) | kHandleBit;
    BuildDecal(self, nullptr, reinterpret_cast<char*>(handle), 0);
    for (int i = 0; i < kGroupCount; ++i) {
        const uint32_t index = group[1 + i];
        if (index == kNone || At<uint8_t>(self, kWaterRows))
            continue;
        BucketInsert(self, index);
        const int32_t* const cells = &At<int32_t>(Overlay(self, index), kCells);
        if (cells[2] <= cells[0] || cells[3] <= cells[1])
            continue;
        int32_t rect[4] = {cells[0], cells[1], cells[2], cells[3]};
        Virtual<void>(At<void*>(self, kTerrain), 0x110, 0u, static_cast<uint32_t>(At<uint8_t>(self, kWaterVertices)),
                      static_cast<void*>(rect), 0u);
    }
    const uint32_t id = NewHandle(self + kHandles, nullptr, reinterpret_cast<const char*>(handle));
    TextureRelease(texture);
    return id | kHandleBit;
}

// 0x0073B8A0, ret 0x10 (texture, int32[4] cells, float[16] matrix, flags): an
// overlay as given, put into its buckets and redrawn. -1 with no texture.
uint32_t __fastcall AddOverlay(char* self, void*, void* texture, const int32_t* cells, const float* matrix, uint32_t flags)
{
    if (!texture)
        return kNone;
    alignas(4) char record[kOverlaySize] = {};
    ResetOverlay(record);
    if (texture != At<void*>(record, kTexture)) {
        void* const old = At<void*>(record, kTexture);
        TextureAddRef(texture);
        At<void*>(record, kTexture) = texture;
        if (old)
            TextureRelease(old);
    }
    std::memcpy(record + kMatrix, matrix, 16 * sizeof(float));
    std::memcpy(record + kCells, cells, 4 * sizeof(int32_t));
    At<uint32_t>(record, kFlags) |= flags;
    const uint32_t index = NewOverlay(self + kOverlays, nullptr, record);
    if (index != kNone && !At<uint8_t>(self, kWaterRows)) {
        BucketInsert(self, index);
        Invalidate(self, index);
    }
    ReleaseRecord(record);
    return index;
}

// Slot 8, 0x0073C9A0, ret 4 (texture ID): a height map over the whole
// terrain (flags 0x12); its matrix has 2 / the cells across in the second
// row's second place and 1 in the last two places of the diagonal.
uint32_t __fastcall AddHeightMap(char* self, void*, uint32_t iid)
{
    const int32_t across = At<int32_t>(self, kCellsAcross);
    float matrix[16] = {};
    matrix[5] = static_cast<float>(static_cast<double>(kTwo) / across);
    matrix[10] = kOne;
    matrix[15] = kOne;
    const int32_t cells[4] = {0, 0, across, At<int32_t>(self, kCellsDown)};
    void* texture = nullptr;
    Virtual<void>(At<void*>(self, kTextures), 0x14, iid, static_cast<void*>(&texture));
    const uint32_t index = AddOverlay(self, nullptr, texture, cells, matrix, 0x12);
    if (texture)
        TextureRelease(texture);
    return index;
}

// Slot 9, 0x0073B3B0, ret 0x14 (texture key, position, size, turn, bool
// mirror): a shadow decal (flags 8; none while the manager has no shadow
// stages), built as a decal. Mirrored, each row of its first matrix gets its
// fourth value less its second in place of the second. Its second matrix is
// the first halved in the first two columns, with a quarter of the fourth
// added to them. Placed (part 1's Place) and given a handle.
uint32_t __fastcall AddShadowDecalKey(char* self, void*, const uint32_t* key, const float* position, float size, float turn,
                                      uint32_t mirror)
{
    if (!At<int32_t>(self, kShadowStages))
        return kNone;
    uint32_t handle[kHandleSize / 4] = {};
    StartHandle(handle, position, size, turn);
    void* texture = nullptr;
    Virtual<void>(At<void*>(self, kTextures), 0x18, key, static_cast<void*>(&texture));
    if (!texture)
        return kNone;
    handle[kAspect / 4] = Bits(TextureAspect(self, texture));
    alignas(4) char record[kOverlaySize] = {};
    ResetOverlay(record);
    SetTexture(record, texture);
    At<uint32_t>(record, kFlags) |= 0x8;
    handle[kTarget / 4] = NewOverlay(self + kOverlays, nullptr, record);
    BuildDecal(self, nullptr, reinterpret_cast<char*>(handle), 0);
    const uint32_t index = handle[kTarget / 4];
    float* const m = &At<float>(Overlay(self, index), kMatrix);

    if (mirror & 0xFF) {
        const auto less = [](float a, float b) { return static_cast<float>(static_cast<double>(a) + -static_cast<double>(b)); };
        const float m1 = m[1], m3 = m[3], m5 = m[5], m7 = m[7], m9 = m[9], m11 = m[11], m13 = m[13], m15 = m[15];
        m[9] = less(m11, m9);
        m[13] = less(m15, m13);
        m[1] = less(m3, m1);
        m[5] = less(m7, m5);
    }

    // Row by row, from the last: (a, b, c, d) gives (a / 2 + d / 4, b / 2 +
    // d / 4, c, d), with the products by zero the game's matrix code keeps.
    float* const second = m + 16;
    for (int i = 3; i >= 0; --i) {
        const float a = m[4 * i], b = m[4 * i + 1], c = m[4 * i + 2], d = m[4 * i + 3];
        const float zeroC = static_cast<float>(static_cast<double>(c) * kZero);
        const float quarter = static_cast<float>(static_cast<double>(d) * kQuarter + zeroC);
        const float zeroB = static_cast<float>(static_cast<double>(b) * kZero);
        const float zeroA = static_cast<float>(static_cast<double>(a) * kZero);
        second[4 * i] = static_cast<float>((static_cast<double>(a) * kHalf + zeroB) + quarter);
        second[4 * i + 1] = static_cast<float>((static_cast<double>(b) * kHalf + zeroA) + quarter);
        second[4 * i + 2] = i == 0 ? static_cast<float>(((static_cast<double>(d) * kZero + zeroA) + c) + zeroB)
                                   : static_cast<float>(((static_cast<double>(d) * kZero + c) + zeroA) + zeroB);
        second[4 * i + 3] = static_cast<float>(((static_cast<double>(zeroA) + zeroB) + zeroC) + d);
    }

    This<void>(addr::cSC4OverlayManager_Place, self, index);
    const uint32_t id = NewHandle(self + kHandles, nullptr, reinterpret_cast<const char*>(handle));
    ReleaseRecord(record);
    TextureRelease(texture);
    return id | kHandleBit;
}

// Slot 10, 0x00735480, ret 0x14: a shadow decal by texture ID, through slot 9
// with the key {FSH, the texture manager's group (its slot 0x2C), ID}.
uint32_t __fastcall AddShadowDecalIid(char* self, void*, uint32_t iid, const float* position, float size, float turn,
                                      uint32_t mirror)
{
    if (!At<int32_t>(self, kShadowStages))
        return kNone;
    const uint32_t group = Virtual<uint32_t>(At<void*>(self, kTextures), 0x2C);
    const uint32_t key[3] = {kFshType, group, iid};
    return Virtual<uint32_t>(self, 0x24, static_cast<const void*>(key), position, size, turn, mirror);
}

// Slot 11, 0x0073C0D0, ret 0x18 (texture, count, float[3] points, float[2]
// points, float[6] bounds, transform): a shadow (flags 8; none while the
// manager has no shadow stages). The lighting object's light projects the
// points into the first matrix (its slot 0x44; nothing when that fails);
// with five stages or more it gives the cells too (0x48), else they come from
// the bounds and the light's direction (0x34, then 0x007354E0). The second
// matrix is the first times the scale and shift that put the ground points'
// bounds in the middle half of the texture.
uint32_t __fastcall AddShadow(char* self, void*, void* texture, int32_t count, const float* points3, const float* points2,
                              const float* box, const void* transform)
{
    if (!At<int32_t>(self, kShadowStages))
        return kNone;
    uint32_t matrix[16];
    const uint32_t unset = Global<uint32_t>(addr::STE_UnsetMatrixFloat);
    for (uint32_t& value : matrix)
        value = unset;
    void* const light = Virtual<void*>(Global<void*>(addr::SC4_Lighting43DD0), 0x24);
    if (!(Virtual<uint32_t>(light, 0x44, count, points3, points2, transform, static_cast<void*>(matrix)) & 0xFF))
        return kNone;
    int32_t cells[4];
    char* const app = Global<char*>(addr::SC4_App43CC4);
    if (At<int32_t>(At<char*>(app, 0x18), 0x1FC) >= 5) {
        if (!(Virtual<uint32_t>(light, 0x48, count, points3, box, static_cast<void*>(cells)) & 0xFF))
            return kNone;
    } else {
        float direction[16];
        Virtual<void>(light, 0x34, static_cast<void*>(direction));
        ShadowCells(self, nullptr, direction, static_cast<uint32_t>(count), points3, box, cells);
    }
    const float* const m = reinterpret_cast<const float*>(matrix);
    const uint32_t index = AddOverlay(self, nullptr, texture, cells, m, 8);
    if (index == kNone)
        return index;

    float minX = points2[0], minZ = points2[1], maxX = points2[0], maxZ = points2[1];
    for (int32_t i = 1; i < count; ++i) {
        const float x = points2[2 * i], z = points2[2 * i + 1];
        if (x < minX)
            minX = x;
        if (z < minZ)
            minZ = z;
        if (maxX < x)
            maxX = x;
        if (maxZ < z)
            maxZ = z;
    }
    const float spanX = static_cast<float>(static_cast<double>(maxX) - minX);
    const double spanZ = static_cast<double>(maxZ) - minZ;
    const float twiceZ = static_cast<float>(spanZ + spanZ);
    const double inverseX = kOneDouble / (static_cast<double>(spanX) + spanX);
    const float scaleX = static_cast<float>(inverseX);
    const float shiftX = static_cast<float>(kQuarterDouble - inverseX * minX);
    const double inverseZ = kOneDouble / twiceZ;
    const float scaleZ = static_cast<float>(inverseZ);
    const float shiftZ = static_cast<float>(kQuarterDouble - static_cast<double>(minZ) * inverseZ);

    // Each row (a, b, c, d) gives (a sx + d tx, b sz + d tz, c, d), with the
    // products by zero kept.
    float* const second = &At<float>(Overlay(self, index), kSecondMatrix);
    for (int i = 3; i >= 0; --i) {
        const float a = m[4 * i], b = m[4 * i + 1], c = m[4 * i + 2], d = m[4 * i + 3];
        const float zeroC = static_cast<float>(static_cast<double>(c) * kZero);
        const float zeroB = static_cast<float>(static_cast<double>(b) * kZero);
        const float zeroA = static_cast<float>(static_cast<double>(a) * kZero);
        second[4 * i] = static_cast<float>(((static_cast<double>(a) * scaleX + static_cast<double>(d) * shiftX) + zeroB) + zeroC);
        second[4 * i + 1] = static_cast<float>(((static_cast<double>(d) * shiftZ + static_cast<double>(b) * scaleZ) + zeroA) + zeroC);
        second[4 * i + 2] = static_cast<float>(((static_cast<double>(d) * kZero + c) + zeroA) + zeroB);
        second[4 * i + 3] = static_cast<float>(((static_cast<double>(d) + zeroA) + zeroB) + zeroC);
    }
    return index;
}

} // namespace opensc4::overlay_manage::part2
