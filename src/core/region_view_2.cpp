// The region map window (docs/region.md), part 2: 0x007B3300 to 0x007B4150.
// Replacement, off by default:
//
//   region.view.part2: the two picture passes a tile's picture goes through
//     before it is put on the map (the copy with the border tint, the
//     outline with its runs), the tile under a point, the map window's
//     constructor and the emptying of its tile list, the cell cache's move
//     to a new view position, the vector of cache cells (construct, copy,
//     fill, erase, grow), the name label's window procedure object
//     (constructor, destructors) and four thunks.
//
// The map window (cSC4WinRegionView, 0x128 bytes, window base class, main
// vtable 0x00AB9658) as far as this part shows it: +0xD8 the cell painter
// interface (vtable 0x00AB9644, its slot 3 is 0x007B4150), +0xE0 a dword,
// +0xE4 the tile record under the pointer, +0xE8 and +0xEC the view's place,
// +0xF0 and +0xF4 two IDs, +0x100 a vector of tile record pointers, +0x10C
// the cell cache, +0x110 to +0x116 flag bytes, +0x118 the map mode, +0x11C a
// vector.
//
// The cell cache (0x44 bytes, vtables 0x00AB9630 and 0x00AB9618 at +4, made
// by the window's Init): +0xC a vector of 8-byte cells (a counted pointer to
// the cell's picture and a byte, set when the cell must be painted again),
// +0x18 and +0x1C the cell the view's corner is in, +0x20 and +0x24 the
// corner's place inside that cell, +0x28 and +0x2C a cell's width and height,
// +0x30 and +0x34 the number of columns and rows, +0x40 a counted pointer.
//
// The label procedure (0x14 bytes, vtables 0x00AB95F0 and 0x00AB9604 at +4):
// the window procedure object the map window gives each tile's name label;
// +0x10 a counted pointer.
//
// A tile record (0x80 bytes, the region screen's): +0x1C its picture, +0x34
// whether its map picture is made, +0x44 the runs of its mask, +0x70 a list
// of visual effects.
//
// Still the game's: 0x007B3BD0 (the destructor of a vector of cells, shared
// with an unrelated class at 0x00491C90). 0x007B3CC0 is not a function: it
// is a branch inside 0x007B3C10 that is never taken.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::region {
namespace {

namespace view { // the map window, by offset
constexpr std::size_t kPainter = 0xD8;
constexpr std::size_t kHover = 0xE4;
constexpr std::size_t kRecords = 0x100; // begin, end, capacity
constexpr std::size_t kCache = 0x10C;
} // namespace view

namespace cache { // the cell cache, by offset
constexpr std::size_t kCells = 0xC; // begin, end, capacity
constexpr std::size_t kCellX = 0x18;
constexpr std::size_t kCellY = 0x1C;
constexpr std::size_t kInsideX = 0x20;
constexpr std::size_t kInsideY = 0x24;
constexpr std::size_t kCellWidth = 0x28;
constexpr std::size_t kCellHeight = 0x2C;
constexpr std::size_t kColumns = 0x30;
constexpr std::size_t kRows = 0x34;
} // namespace cache

namespace record { // a tile record, by offset
constexpr std::size_t kPicture = 0x1C;
constexpr std::size_t kComposed = 0x34;
constexpr std::size_t kMaskRuns = 0x44; // begin, end
constexpr std::size_t kEffects = 0x70;
} // namespace record

namespace image { // the game's image buffer, by vtable offset
constexpr std::size_t kLock = 0x18;   // (flags)
constexpr std::size_t kUnlock = 0x1C; // (flags)
constexpr std::size_t kArea = 0x30;   // -> int32_t[4]: left, top, right, bottom
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace image

struct DwordVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};
static_assert(sizeof(DwordVector) == 0xC);

// A cell of the cache: only the byte at +4 is ever copied.
struct Cell {
    void* picture;
    uint8_t stale;
};
static_assert(sizeof(Cell) == 8);

struct CellVector {
    Cell* begin;
    Cell* end;
    Cell* capacity;
};
static_assert(sizeof(CellVector) == 0xC);

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// 32-bit arithmetic that wraps as the game's does.
int32_t Add(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t Sub(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

int32_t Mul(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

// cdq, xor, sub.
int32_t Abs(int32_t value)
{
    const uint32_t sign = static_cast<uint32_t>(value >> 31);
    return static_cast<int32_t>((static_cast<uint32_t>(value) ^ sign) - sign);
}

uint32_t* RowOf(void* picture, int32_t row)
{
    const int32_t stride = Virtual<int32_t>(picture, image::kStride);
    const int32_t offset = Mul(stride, row);
    return reinterpret_cast<uint32_t*>(
        static_cast<std::uintptr_t>(static_cast<uint32_t>(Add(offset, Virtual<int32_t>(picture, image::kBits)))));
}

// One dword at the end of a vector, through the game's insert when it is
// full. `unused` is what the original passes where the insert takes an
// allocator: the address of its own first argument's slot.
void PushBack(DwordVector* vector, uint32_t value, void* unused)
{
    uint32_t* const end = vector->end;
    if (end != vector->capacity) {
        if (end)
            *end = value;
        ++vector->end;
        return;
    }
    This<void>(addr::SC4PtrVector_InsertN, vector, end, &value, unused, uint32_t{1}, uint32_t{1});
}

// The game's 0x007B3300 (the source picture in EAX; on the stack the target
// picture, a vector of runs with values or null, a colour; cleaned by the
// caller). Copies the source's pixels to the target, row by row, and then
// blends the colour's three low bytes into the target along the runs: each
// run is the place of its first pixel (y * 0x10000 + x), the place after its
// last, and one dword per pixel whose low byte, halved, is the weight of the
// colour in 256ths. The alpha byte is kept. The target is locked with flags
// 0x8080 and unlocked; the source is locked with 0x800 and not unlocked. The
// size copied is the source's (the original asks the source for its area
// twice and takes the smaller of the two).
void __cdecl CopyAndTint(void* source, void* target, DwordVector* runs, uint32_t colour)
{
    if (!Virtual<uint8_t>(target, image::kLock, uint32_t{0x8080}))
        return;
    if (Virtual<uint8_t>(source, image::kLock, uint32_t{0x800})) {
        const int32_t* const first = Virtual<const int32_t*>(source, image::kArea);
        const int32_t* const second = Virtual<const int32_t*>(source, image::kArea);
        const int32_t secondWidth = Sub(second[2], second[0]);
        const int32_t firstWidth = Sub(first[2], first[0]);
        const int32_t width = secondWidth < firstWidth ? secondWidth : firstWidth;
        const int32_t secondHeight = Sub(second[3], second[1]);
        const int32_t firstHeight = Sub(first[3], first[1]);
        const int32_t height = secondHeight < firstHeight ? secondHeight : firstHeight;
        if (height > 0) {
            const uint32_t bytes = static_cast<uint32_t>(width) * 4;
            int32_t row = 0;
            do {
                const uint32_t* const from = RowOf(source, row);
                uint32_t* const to = RowOf(target, row);
                std::memcpy(to, from, bytes);
                ++row;
            } while (row < height);
        }
        if (runs && runs->begin != runs->end) {
            Virtual<const int32_t*>(source, image::kArea);
            const int32_t colour1 = static_cast<int32_t>((colour >> 8) & 0xFF);
            const int32_t colour0 = static_cast<int32_t>(colour & 0xFF);
            const int32_t colour2 = static_cast<int32_t>((colour >> 16) & 0xFF);
            const uint32_t* at = runs->begin;
            const uint32_t* const last = runs->end - 1;
            while (at <= last) {
                const uint32_t start = at[0];
                const uint32_t end = at[1];
                at += 2;
                const int32_t stride = Virtual<int32_t>(target, image::kStride);
                const int32_t offset = Mul(stride, static_cast<int32_t>(start >> 16));
                uint32_t* const pixels = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(
                    static_cast<uint32_t>(Add(offset, Virtual<int32_t>(target, image::kBits)))));
                int32_t x = static_cast<int32_t>(start & 0xFFFF);
                const int32_t endX = static_cast<int32_t>(end & 0xFFFF);
                while (x < endX) {
                    const int32_t weight = static_cast<uint8_t>(*at) >> 1;
                    ++at;
                    if (weight != 0) {
                        const uint32_t pixel = pixels[x];
                        const int32_t pixel2 = static_cast<int32_t>((pixel >> 16) & 0xFF);
                        const int32_t pixel1 = static_cast<int32_t>((pixel >> 8) & 0xFF);
                        const int32_t pixel0 = static_cast<int32_t>(pixel & 0xFF);
                        const int32_t value2 = (((colour2 - pixel2) * weight + 0x80) >> 8) + pixel2;
                        const int32_t value1 = (((colour1 - pixel1) * weight + 0x80) >> 8) + pixel1;
                        const int32_t value0 = (((colour0 - pixel0) * weight + 0x80) >> 8) + pixel0;
                        const uint32_t high = (static_cast<uint32_t>(value2) << 8) + static_cast<uint32_t>(value1);
                        pixels[x] = (pixel & 0xFF000000) + ((high << 8) + static_cast<uint32_t>(value0));
                    }
                    ++x;
                }
            }
        }
    }
    Virtual<void>(target, image::kUnlock, uint32_t{0x8080});
}

__declspec(naked) void CopyAndTintThunk()
{
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm push eax
    __asm call CopyAndTint
    __asm add esp, 16
    __asm ret
}

// The game's 0x007B3560 (this is 0x14 bytes of new memory, no arguments):
// the constructor of the label procedure. Returns this.
void* __fastcall LabelProcConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4NewsTicker_WinProcBaseVtable;
    This<void*>(addr::cRZBaseRefCount_Construct, self + 4);
    At<std::uintptr_t>(self, 4) = addr::SC4RegionViewLabelProc_CountVtable;
    At<std::uintptr_t>(self, 0) = addr::SC4RegionViewLabelProc_Vtable;
    At<void*>(self, 0x10) = nullptr;
    return self;
}

// The game's 0x007B35C0 (this is the label procedure, no arguments): its
// destructor. Releases the pointer at +0x10 and ends in the counted base's
// destructor.
void* __fastcall LabelProcDestruct(char* self, void*)
{
    if (void* const object = At<void*>(self, 0x10))
        Release(object);
    return This<void*>(addr::cRZBaseRefCount_Destruct, self ? self + 4 : nullptr);
}

// The game's 0x007B35A0 (this is the label procedure; (flags), ret 4): the
// deleting destructor. Returns this.
void* __fastcall LabelProcDelete(char* self, void*, uint32_t flags)
{
    LabelProcDestruct(self, nullptr);
    if (static_cast<uint8_t>(flags) & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007B3590, slot 0 of the label procedure's vtable at +4: the
// deleting destructor with `this` moved back.
void* __fastcall LabelProcDeleteThunk(char* self, void*, uint32_t flags)
{
    return LabelProcDelete(self - 4, nullptr, flags);
}

// A cell made in raw memory from another.
void ConstructCell(Cell* to, const Cell* from)
{
    void* const picture = from->picture;
    to->picture = picture;
    if (picture)
        AddRef(picture);
    to->stale = from->stale;
}

// The game's 0x007B35F0 (cdecl: first, last, to, an unused pointer). Copies
// cells into raw memory; returns the end of the copy.
Cell* __cdecl CopyCells(Cell* first, Cell* last, Cell* to, void*)
{
    while (first != last) {
        if (to)
            ConstructCell(to, first);
        ++first;
        ++to;
    }
    return to;
}

// The game's 0x007B3630 (cdecl: to, count, value, an unused pointer). Fills
// raw memory with copies of a cell; returns the end.
Cell* __cdecl FillCells(Cell* to, uint32_t count, const Cell* value, void*)
{
    if (count != 0) {
        do {
            if (to)
                ConstructCell(to, value);
            ++to;
            --count;
        } while (count != 0);
    }
    return to;
}

// One channel of one pixel laid under another: the lower one's value times
// what the upper one lets through, in 256ths and rounded, plus the upper's.
uint32_t Under(uint32_t lower, uint32_t through, uint32_t upper)
{
    return ((lower * through + 0x80) >> 8) + upper;
}

// The game's 0x007B3670 (cdecl: picture, vector of runs). Gives the
// picture's pixels that are not opaque what lies under them, taken from the
// pixel below laid over the pixel to the right (nothing beyond the last
// column, a row of zeros beyond the last row), in place and in reading
// order, so the picture's edge is repeated one pixel up and to the left.
// Meanwhile the vector is emptied and filled with the places (y * 0x10000 +
// x) where a row's pixels change between clear, partly opaque and opaque,
// each stretch closed where the next begins and at the row's end. Nothing is
// done to a picture without width or height, nor when it cannot be locked
// (flags 0x8040).
void __cdecl OutlineAndCollect(void* picture, DwordVector* runs)
{
    const int32_t* const area = Virtual<const int32_t*>(picture, image::kArea);
    const int32_t width = Sub(area[2], area[0]);
    const int32_t height = Sub(area[3], area[1]);
    if (width <= 0 || height <= 0)
        return;
    runs->end = runs->begin;
    if (!Virtual<uint8_t>(picture, image::kLock, uint32_t{0x8040}))
        return;
    DwordVector blank;
    This<void*>(addr::SC4DwordVector_ConstructZeroed, static_cast<void*>(&blank), width);
    uint32_t zero = 0;
    Cdecl<void>(addr::SC4DwordVector_FillRange, blank.begin, blank.end, &zero);
    uint32_t base = 0;
    int32_t y = 0;
    do {
        uint32_t* const row = RowOf(picture, y);
        const uint32_t* below;
        if (y < height - 1)
            below = RowOf(picture, y + 1);
        else
            below = blank.begin;
        int32_t state = 0; // 0 clear, 1 partly opaque, 2 opaque
        int32_t x = 0;
        do {
            const uint32_t pixel = row[x];
            uint32_t alpha = pixel >> 24;
            if (pixel < 0xFF000000u) {
                const uint32_t under = below[x];
                const uint32_t right = x < width - 1 ? row[x + 1] : 0;
                const uint32_t keep = ~under >> 24;
                const uint32_t behind3 = Under(right >> 24, keep, under >> 24);
                const uint32_t behind2 = Under((right >> 16) & 0xFF, keep, (under >> 16) & 0xFF);
                const uint32_t behind1 = Under((right >> 8) & 0xFF, keep, (under >> 8) & 0xFF);
                const uint32_t behind0 = Under(right & 0xFF, keep, under & 0xFF);
                const uint32_t show = ~pixel >> 24;
                uint32_t value3 = Under(show, behind3, alpha);
                uint32_t value2 = Under(show, behind2, (pixel >> 16) & 0xFF);
                uint32_t value1 = Under(show, behind1, (pixel >> 8) & 0xFF);
                uint32_t value0 = Under(show, behind0, pixel & 0xFF);
                if (value3 > 0xFF)
                    value3 = 0xFF;
                if (value2 > 0xFF)
                    value2 = 0xFF;
                if (value1 > 0xFF)
                    value1 = 0xFF;
                if (value0 > 0xFF)
                    value0 = 0xFF;
                row[x] = ((((((value3 << 8) + value2) << 8) + value1) << 8) + value0);
                alpha = value3;
            }
            if (alpha != 0) {
                const int32_t kind = alpha < 0xFF ? 1 : 2;
                if (state != kind) {
                    if (state != 0)
                        PushBack(runs, base + static_cast<uint32_t>(x), &picture);
                    PushBack(runs, base + static_cast<uint32_t>(x), &picture);
                    state = kind;
                }
            } else if (state != 0) {
                state = 0;
                PushBack(runs, base + static_cast<uint32_t>(x), &picture);
            }
            ++x;
        } while (x < width);
        if (state != 0)
            PushBack(runs, static_cast<uint32_t>(x) + base, &picture);
        ++y;
        base += 0x10000;
    } while (y < height);
    Virtual<void>(picture, image::kUnlock, uint32_t{0x8040});
    if (blank.begin)
        Free(blank.begin);
}

// The game's 0x007B3A80 (this is the map window; (x, y), ret 8). The tile
// record under a point of the window, or null: the records are tried from
// the last to the first; the point must lie within the record's picture
// where the window shows it (0x007B3030 gives its corner) and inside one of
// the runs of its mask (a binary search for y * 0x10000 + x + 1 that ends on
// an odd index).
char* __fastcall TileAt(char* self, void*, int32_t x, int32_t y)
{
    int32_t index =
        static_cast<int32_t>((At<char*>(self, view::kRecords + 4) - At<char*>(self, view::kRecords)) >> 2) - 1;
    if (index < 0)
        return nullptr;
    do {
        char* const tile = At<char**>(self, view::kRecords)[index];
        int32_t left;
        int32_t top;
        This<void>(addr::cSC4WinRegionView_TilePlace, self, tile, &left, &top);
        const int32_t inX = Sub(x, left);
        const int32_t inY = Sub(y, top);
        const int32_t* const area = Virtual<const int32_t*>(At<void*>(tile, record::kPicture), image::kArea);
        if (inX >= area[0] && inY >= area[1] && inX < area[2] && inY < area[3]) {
            const int32_t key =
                static_cast<int32_t>((static_cast<uint32_t>(inY) << 16) + static_cast<uint32_t>(inX) + 1);
            const char* const runsEnd = At<char*>(tile, record::kMaskRuns + 4);
            const int32_t* const first = At<int32_t*>(tile, record::kMaskRuns);
            const int32_t* at = first;
            int32_t count = static_cast<int32_t>((runsEnd - reinterpret_cast<const char*>(first)) >> 2);
            while (count > 0) {
                const int32_t half = count >> 1;
                if (at[half] < key) {
                    count += -1 - half;
                    at += half + 1;
                } else {
                    count = half;
                }
            }
            const uint32_t bytes =
                static_cast<uint32_t>(reinterpret_cast<const char*>(at) - reinterpret_cast<const char*>(first));
            if (bytes & 4)
                return tile;
        }
        --index;
    } while (index >= 0);
    return nullptr;
}

// The game's 0x007B3B60 (this is three dwords of raw memory; (count),
// ret 4). A vector of `count` cells without a picture. Returns this. The
// original copies each cell's byte from a stack slot it never wrote; here it
// is 0. The one caller writes every byte afterwards.
CellVector* __fastcall ConstructCells(CellVector* self, void*, uint32_t count)
{
    self->begin = nullptr;
    self->end = nullptr;
    self->capacity = nullptr;
    Cell* memory;
    if (count != 0)
        memory = Cdecl<Cell*>(addr::SC4_stl_allocate, count * 8);
    else
        memory = nullptr;
    self->capacity = memory + count;
    self->begin = memory;
    self->end = memory;
    Cell blank;
    blank.picture = nullptr;
    blank.stale = 0;
    self->end = FillCells(memory, count, &blank, &count);
    return self;
}

// The game's 0x007B3C10 (this is the map window, no arguments). Forgets the
// tile records: no tile is under the pointer, every record's effects are
// stopped (slot 0x10 with 0) and its list of them emptied, the window's list
// of records is emptied, every cell of the cache is marked stale, and the
// 3D view is asked for a full redraw (slot 0x50), whose answer is returned.
// The original then walks the list it has just emptied to clear each
// record's byte +0x34: kept, though it finds nothing.
uint8_t __fastcall ForgetRecords(char* self, void*)
{
    char** tile = At<char**>(self, view::kRecords);
    char** const tilesEnd = At<char**>(self, view::kRecords + 4);
    At<void*>(self, view::kHover) = nullptr;
    for (; tile != tilesEnd; ++tile) {
        char* const item = *tile;
        char* const sentinel = At<char*>(item, record::kEffects);
        for (char* node = At<char*>(sentinel, 0); node != sentinel; node = At<char*>(node, 0))
            Virtual<void>(At<void*>(node, 8), 0x10, uint32_t{0}); // Stop
        char* node = At<char*>(At<char*>(item, record::kEffects), 0);
        if (node != At<char*>(item, record::kEffects)) {
            do {
                char* const dead = node;
                void* const effect = At<void*>(dead, 8);
                node = At<char*>(node, 0);
                if (effect)
                    Release(effect);
                Free(dead);
            } while (node != At<char*>(item, record::kEffects));
        }
        char* const head = At<char*>(item, record::kEffects);
        At<char*>(head, 0) = head;
        char* const again = At<char*>(item, record::kEffects);
        At<char*>(again, 4) = again;
    }
    At<char**>(self, view::kRecords + 4) = At<char**>(self, view::kRecords);

    char* const cells = At<char*>(self, view::kCache);
    Cell* cell = At<Cell*>(cells, cache::kCells);
    Cell* const cellsEnd = At<Cell*>(cells, cache::kCells + 4);
    for (; cell != cellsEnd; ++cell)
        cell->stale = 1;

    char** left = At<char**>(self, view::kRecords);
    char** const leftEnd = At<char**>(self, view::kRecords + 4);
    while (left != leftEnd) {
        char* const item = *left;
        ++left;
        At<uint8_t>(item, record::kComposed) = 0;
    }
    return Virtual<uint8_t>(Global<void*>(addr::SC4_View43DD0), 0x50); // ForceFullRedraw
}

// The game's 0x007B3D30 (this is a vector of cells; (first, last), ret 8).
// Erases the range: the cells after it are moved down by assignment
// (0x007B3220) and the cells left over at the end released. Returns `first`.
Cell* __fastcall EraseCells(CellVector* self, void*, Cell* first, Cell* last)
{
    Cell* const newEnd = Cdecl<Cell*>(addr::SC4RegionViewGrid_CopyCellRange, last, self->end, first, &first, uint32_t{0});
    Cell* const oldEnd = self->end;
    for (Cell* at = newEnd; at != oldEnd; ++at) {
        if (void* const picture = at->picture)
            Release(picture);
    }
    Cell* const result = first;
    self->end = newEnd;
    return result;
}

// The game's 0x007B3D80 (this is a vector of cells; (where, value, an unused
// pointer, count, at end), ret 0x14). Inserts `count` copies of a cell when
// the vector must grow: new storage for the old size plus the larger of the
// old size and `count`, the cells before `where`, the copies, the cells
// after it unless `at end` is set; the old cells are released and their
// storage freed (0x007B3BD0).
void __fastcall InsertCellsGrowing(CellVector* self, void*, Cell* where, const Cell* value, void*, uint32_t count,
                                   uint32_t atEnd)
{
    const uint32_t size = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(self->end) - reinterpret_cast<char*>(self->begin)) >> 3);
    const uint32_t capacity = (size < count ? count : size) + size;
    Cell* fresh;
    if (capacity != 0)
        fresh = Cdecl<Cell*>(addr::SC4_stl_allocate, capacity * 8);
    else
        fresh = nullptr;
    Cell* at = CopyCells(self->begin, where, fresh, &atEnd);
    if (count == 1) {
        if (at)
            ConstructCell(at, value);
        ++at;
    } else {
        at = FillCells(at, count, value, &atEnd);
    }
    if (static_cast<uint8_t>(atEnd) == 0)
        at = CopyCells(where, self->end, at, &atEnd);
    This<void>(addr::SC4RegionViewGrid_DestroyCells, static_cast<void*>(self));
    self->begin = fresh;
    self->end = at;
    self->capacity = fresh + capacity;
}

// The game's 0x007B3E60, slot 0 of the cell cache's vtable at +4:
// QueryInterface (0x007B23E0; (iid, out), ret 8) with `this` moved back.
uint8_t __fastcall CacheQueryInterfaceThunk(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4RegionViewHelper_QueryInterface, static_cast<void*>(self - 4), iid, out);
}

// The game's 0x007B3E70, slot 3 of the cell cache's vtable at +4: the
// deleting destructor (0x007B5300; (flags), ret 4) with `this` moved back.
void* __fastcall CacheDeleteThunk(char* self, void*, uint32_t flags)
{
    return This<void*>(addr::SC4RegionViewGrid_DeletingDestructor, static_cast<void*>(self - 4), flags);
}

// The quotient rounded down for a negative numerator, as the original has it.
int32_t CellOf(int32_t place, int32_t size)
{
    int32_t cell = place / size;
    if (place < 0 && Mul(cell, size) != place)
        --cell;
    return cell;
}

// The game's 0x007B3E80 (this is the cell cache; (x, y), ret 8). Moves the
// cache to a new view position. The cell the position falls in is found;
// when that is fewer columns and rows away from the old one than the cache
// has, a new vector of cells is made in which every cell takes the picture
// and the byte of the cell that many columns and rows further on, wrapping
// round, and the cells that wrapped are marked stale; the old vector is
// released. Otherwise every cell is marked stale. Then the cell and the
// position's place inside it are stored. EAX is left holding the cell's row.
int32_t __fastcall CacheMoveTo(char* self, void*, int32_t x, int32_t y)
{
    const int32_t cellX = CellOf(x, At<int32_t>(self, cache::kCellWidth));
    const int32_t cellY = CellOf(y, At<int32_t>(self, cache::kCellHeight));
    const int32_t moveX = Sub(cellX, At<int32_t>(self, cache::kCellX));
    const int32_t moveY = Sub(cellY, At<int32_t>(self, cache::kCellY));
    CellVector& cells = At<CellVector>(self, cache::kCells);
    if (Abs(moveX) < At<int32_t>(self, cache::kColumns) && Abs(moveY) < At<int32_t>(self, cache::kRows)) {
        CellVector fresh;
        ConstructCells(&fresh, nullptr,
                       static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<char*>(cells.end) -
                                                                  reinterpret_cast<char*>(cells.begin)) >> 3));
        int32_t row = 0;
        if (At<int32_t>(self, cache::kRows) > 0) {
            do {
                int32_t column = 0;
                if (At<int32_t>(self, cache::kColumns) > 0) {
                    const int32_t wantedRow = Add(row, moveY);
                    do {
                        const int32_t columns = At<int32_t>(self, cache::kColumns);
                        const int32_t fromColumn = Add(Add(column, moveX), columns) % columns;
                        const int32_t rows = At<int32_t>(self, cache::kRows);
                        const int32_t fromRow = Add(rows, wantedRow) % rows;
                        const int32_t toIndex = Add(Mul(columns, row), column);
                        const int32_t fromIndex = Add(Mul(columns, fromRow), fromColumn);
                        const Cell* const from = cells.begin + fromIndex;
                        Cell* const to = fresh.begin + toIndex;
                        if (from != to) {
                            void* const taken = from->picture;
                            void* const old = to->picture;
                            if (taken != old) {
                                if (taken)
                                    AddRef(taken);
                                to->picture = taken;
                                if (old)
                                    Release(old);
                            }
                        }
                        to->stale = from->stale;
                        if (Add(column, moveX) != fromColumn || wantedRow != fromRow)
                            fresh.begin[Add(Mul(At<int32_t>(self, cache::kColumns), row), column)].stale = 1;
                        ++column;
                    } while (column < At<int32_t>(self, cache::kColumns));
                }
                ++row;
            } while (row < At<int32_t>(self, cache::kRows));
        }
        Cell* const oldBegin = cells.begin;
        cells.begin = fresh.begin;
        Cell* const oldEnd = cells.end;
        cells.end = fresh.end;
        cells.capacity = fresh.capacity;
        for (Cell* at = oldBegin; at != oldEnd; ++at) {
            if (void* const picture = at->picture)
                Release(picture);
        }
        if (oldBegin)
            Free(oldBegin);
    } else {
        for (Cell* at = cells.begin; at != cells.end; ++at)
            at->stale = 1;
    }
    const int32_t cellLeft = Mul(At<int32_t>(self, cache::kCellWidth), cellX);
    At<int32_t>(self, cache::kCellX) = cellX;
    const int32_t cellTop = Mul(At<int32_t>(self, cache::kCellHeight), cellY);
    At<int32_t>(self, cache::kInsideX) = Sub(x, cellLeft);
    At<int32_t>(self, cache::kCellY) = cellY;
    At<int32_t>(self, cache::kInsideY) = Sub(y, cellTop);
    return cellY;
}

// The game's 0x007B4090 (this is 0x128 bytes of new memory, no arguments):
// the map window's constructor. Returns this. +0xDC, +0xF8 and +0xFC are
// not set here.
void* __fastcall ViewConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, static_cast<void*>(self));
    At<std::uintptr_t>(self, view::kPainter) = addr::cSC4WinRegionView_PainterBaseVtable;
    At<std::uintptr_t>(self, 0) = addr::cSC4WinRegionView_Vtable;
    At<std::uintptr_t>(self, view::kPainter) = addr::cSC4WinRegionView_PainterVtable;
    At<uint32_t>(self, 0xE0) = 0;
    At<uint32_t>(self, 0xE4) = 0;
    At<uint32_t>(self, 0xE8) = 0;
    At<uint32_t>(self, 0xEC) = 0;
    At<uint32_t>(self, 0xF0) = 0;
    At<uint32_t>(self, 0xF4) = 0;
    At<uint32_t>(self, 0x100) = 0;
    At<uint32_t>(self, 0x104) = 0;
    At<uint32_t>(self, 0x108) = 0;
    At<uint32_t>(self, 0x10C) = 0;
    At<uint8_t>(self, 0x110) = 0;
    At<uint8_t>(self, 0x112) = 0;
    At<uint8_t>(self, 0x114) = 0;
    At<uint8_t>(self, 0x115) = 0;
    At<uint32_t>(self, 0x118) = 0;
    At<uint8_t>(self, 0x111) = 1;
    At<uint8_t>(self, 0x113) = 1;
    At<uint8_t>(self, 0x116) = 1;
    At<uint32_t>(self, 0x11C) = 0;
    At<uint32_t>(self, 0x120) = 0;
    At<uint32_t>(self, 0x124) = 0;
    return self;
}

// The game's 0x007B4140, slot 0 of the cell painter interface's vtable: the
// window's QueryInterface (0x00682050, a jump to the window base class's;
// (iid, out), ret 8) with `this` moved back to the window.
uint8_t __fastcall PainterQueryInterfaceThunk(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4Win_QueryInterfaceJump, static_cast<void*>(self - view::kPainter), iid, out);
}

bool InstallRegionView2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionView_CopyAndTint, reinterpret_cast<void*>(&CopyAndTintThunk)},
        {addr::SC4RegionViewLabelProc_Construct, reinterpret_cast<void*>(&LabelProcConstruct)},
        {addr::SC4RegionViewLabelProc_DeletingDestructorThunk, reinterpret_cast<void*>(&LabelProcDeleteThunk)},
        {addr::SC4RegionViewLabelProc_DeletingDestructor, reinterpret_cast<void*>(&LabelProcDelete)},
        {addr::SC4RegionViewLabelProc_Destruct, reinterpret_cast<void*>(&LabelProcDestruct)},
        {addr::SC4RegionViewGrid_UninitializedCopyCells, reinterpret_cast<void*>(&CopyCells)},
        {addr::SC4RegionViewGrid_UninitializedFillCells, reinterpret_cast<void*>(&FillCells)},
        {addr::cSC4WinRegionView_OutlineAndCollect, reinterpret_cast<void*>(&OutlineAndCollect)},
        {addr::cSC4WinRegionView_TileAt, reinterpret_cast<void*>(&TileAt)},
        {addr::SC4RegionViewGrid_ConstructCells, reinterpret_cast<void*>(&ConstructCells)},
        {addr::cSC4WinRegionView_ForgetRecords, reinterpret_cast<void*>(&ForgetRecords)},
        {addr::SC4RegionViewGrid_EraseCells, reinterpret_cast<void*>(&EraseCells)},
        {addr::SC4RegionViewGrid_InsertCellsGrowing, reinterpret_cast<void*>(&InsertCellsGrowing)},
        {addr::SC4RegionViewGrid_QueryInterfaceThunk, reinterpret_cast<void*>(&CacheQueryInterfaceThunk)},
        {addr::SC4RegionViewGrid_DeletingDestructorThunk, reinterpret_cast<void*>(&CacheDeleteThunk)},
        {addr::SC4RegionViewGrid_MoveTo, reinterpret_cast<void*>(&CacheMoveTo)},
        {addr::cSC4WinRegionView_Construct, reinterpret_cast<void*>(&ViewConstruct)},
        {addr::cSC4WinRegionView_PainterQueryInterfaceThunk, reinterpret_cast<void*>(&PainterQueryInterfaceThunk)},
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

const bool g_regionView2Registered = registry::Register({
    "region.view.part2",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionView_Construct,
    "Our region map window, part 2: a tile picture's copy with the border tint and its outline with runs, the "
    "tile under a point, the constructor, forgetting the tile records, the cell cache's move and its cell vector, "
    "the label procedure object",
    false,
    InstallRegionView2,
});

} // namespace
} // namespace opensc4::region
