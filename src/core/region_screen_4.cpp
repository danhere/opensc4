// The region screen's windows (docs/region.md), part 4: 0x007ADE50 to
// 0x007AEDD0. Replacement, off by default:
//
//   region.screen.part4: the region screen window's work on the cities it
//     shows: the record it keeps per city tile (copying, assigning and the
//     vector helpers), the pictures of a tile shifted by the fraction of its
//     place on the map, the regional city under a tile (made when there is
//     none), the population total, and the ordering of the region's layout
//     tiles.
//
// The window (cSC4WinRegionScreen, main vtable 0x00AB92A0) keeps a vector of
// tile records at +0x118 and the image factory it makes pictures with at
// +0x154. A tile record is 0x80 bytes:
//
//   +0x08, +0x0C  the tile's place in the region, km
//   +0x10, +0x14  its place on the map picture, pixels (floats)
//   +0x18         log2 of its side in km (byte)
//   +0x1C, +0x20  the city's picture and its mask (counted)
//   +0x24, +0x28  a second picture and mask, or null
//   +0x2C         a 32-bit picture of the first one's size
//   +0x30         one more counted picture
//   +0x34         a byte
//   +0x38         a vector of dwords
//   +0x44, +0x5C, +0x50  dword vectors made from the mask's first, second and
//                 third byte (0x007AD400)
//   +0x68, +0x6C  a point worked out from the mask (0x007AA6A0)
//   +0x70         a list of counted objects
//   +0x74         a vector of 12-byte items
//
// The pictures a city file carries are drawn for whole pixels. A tile sits at
// a fraction of a pixel on the map, so each picture is made again two pixels
// larger with every pixel the weighted mean of four of the old one: the
// weights are a tent of width one around the fraction, in 14-bit fixed point.
//
// The layout tiles (12 bytes: x, y, log2 of the side) are sorted by
// x + y + side, then by x: the order the map draws them in. The four sort
// functions are the STL's introsort made for that ordering; the heap and
// partition pieces they call are part 3's and part 1's.
//
// Drawing, the image objects, the window base class, strings and the
// vectors' own code stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace window { // cSC4WinRegionScreen
constexpr std::size_t kChildByID = 0x8C;      // vtable: (id) -> child window
constexpr std::size_t kTilesBegin = 0x118;
constexpr std::size_t kTilesEnd = 0x11C;
constexpr std::size_t kImageFactory = 0x154;
constexpr uint32_t kPopulationText = 0xC9E41918;
} // namespace window

namespace tile { // a tile record of the region screen
constexpr std::size_t kSize = 0x80;
constexpr std::size_t kX = 0x08;
constexpr std::size_t kY = 0x0C;
constexpr std::size_t kMapX = 0x10;
constexpr std::size_t kMapY = 0x14;
constexpr std::size_t kSideLog2 = 0x18;
constexpr std::size_t kPicture = 0x1C;
constexpr std::size_t kMask = 0x20;
constexpr std::size_t kSecondPicture = 0x24;
constexpr std::size_t kSecondMask = 0x28;
constexpr std::size_t kScratch = 0x2C;
constexpr std::size_t kExtraPicture = 0x30;
constexpr std::size_t kFlag = 0x34;
constexpr std::size_t kVector38 = 0x38;
constexpr std::size_t kSpansFirst = 0x44;
constexpr std::size_t kSpansThird = 0x50;
constexpr std::size_t kSpansSecond = 0x5C;
constexpr std::size_t kPointX = 0x68;
constexpr std::size_t kPointY = 0x6C;
constexpr std::size_t kList = 0x70;
constexpr std::size_t kVector74 = 0x74;
} // namespace tile

namespace image { // the game's image buffer, vtable offsets
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kInit = 0xC;      // (width, height, kind, bits per pixel)
constexpr std::size_t kLock = 0x18;     // (flags)
constexpr std::size_t kUnlock = 0x1C;   // (flags)
constexpr std::size_t kWidth = 0x24;
constexpr std::size_t kHeight = 0x28;
constexpr std::size_t kGetArea = 0x2C;  // (int32_t[4] out)
constexpr std::size_t kArea = 0x30;     // -> int32_t[4]: left, top, right, bottom
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace image

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A vector of dwords.
struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};
static_assert(sizeof(Uint32Vector) == 0xC);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A layout tile of the region.
struct Tile {
    int32_t x;
    int32_t y;
    int32_t sideLog2;
};
static_assert(sizeof(Tile) == 0xC);

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

uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, image::kAddRef);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, image::kRelease);
}

void DestroyString(GameString& text)
{
    char* const buffer = text.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

// Empties a counted pointer: the slot is cleared before the release.
void Reset(void** slot)
{
    if (void* const held = *slot) {
        *slot = nullptr;
        Release(held);
    }
}

// ---------------------------------------------------------------------------
// The ordering of layout tiles.
// ---------------------------------------------------------------------------

// The ordering inlined in every sort piece: by x + y + side, then by x.
bool Less(const Tile& a, const Tile& b)
{
    const uint32_t difference = (uint32_t{1} << (a.sideLog2 & 31)) - (uint32_t{1} << (b.sideLog2 & 31)) -
                                static_cast<uint32_t>(b.y) - static_cast<uint32_t>(b.x) +
                                static_cast<uint32_t>(a.y) + static_cast<uint32_t>(a.x);
    if (static_cast<int32_t>(difference) < 0)
        return true;
    return difference == 0 && a.x < b.x;
}

int32_t Count(const Tile* first, const Tile* last)
{
    return static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(last) - reinterpret_cast<std::uintptr_t>(first)) / 12;
}

Tile* Step(Tile* tile, int32_t count)
{
    return reinterpret_cast<Tile*>(reinterpret_cast<std::uintptr_t>(tile) + static_cast<uint32_t>(count) * 12);
}

// The game's 0x007ADE50 (cdecl, five arguments; the fourth is not read, the
// fifth is the empty ordering object passed by value). The STL's partial
// sort: the smallest (middle - first) tiles of [first, last) in order at the
// front, through a heap.
void __cdecl PartialSort(Tile* first, Tile* middle, Tile* last, uint32_t, uint32_t order)
{
    Cdecl<void>(addr::SC4RegionTileSort_MakeHeap, first, middle, order, uint32_t{0}, uint32_t{0});
    for (Tile* tile = middle; reinterpret_cast<std::uintptr_t>(tile) < reinterpret_cast<std::uintptr_t>(last);
         tile = Step(tile, 1)) {
        if (Less(*tile, *first)) {
            const int32_t x = tile->x;
            const int32_t y = tile->y;
            const int32_t sideLog2 = tile->sideLog2;
            *tile = *first;
            Cdecl<void>(addr::SC4RegionTileSort_AdjustHeap, first, int32_t{0}, Count(first, middle), x, y, sideLog2, order);
        }
    }
    Cdecl<void>(addr::SC4RegionTileSort_SortHeap, first, middle, order);
}

// The game's 0x007ADF40 (cdecl, three arguments). The STL sort's last pass,
// an insertion sort: the first sixteen tiles with the checked insert, the
// rest with the unchecked one.
void __cdecl InsertionSort(Tile* first, Tile* last, uint32_t order)
{
    if (Count(first, last) <= 0x10) {
        Cdecl<void>(addr::SC4RegionTileSort_InsertionSort, first, last, order);
        return;
    }
    Tile* const middle = Step(first, 0x10);
    Cdecl<void>(addr::SC4RegionTileSort_InsertionSort, first, middle, order);
    Cdecl<void>(addr::SC4RegionTileSort_InsertionPass, middle, last, uint32_t{0}, order);
}

// The game's 0x007AE8D0 (cdecl, five arguments; the third is not read). The
// STL sort's main loop: while more than sixteen tiles are left, split around
// the median of the first, middle and last tile, sort the upper part by a
// call to itself and go on with the lower; when the depth allowance is used
// up, a heap sort instead.
void __cdecl IntroSort(Tile* first, Tile* last, uint32_t, int32_t depth, uint32_t order)
{
    if (Count(first, last) <= 0x10)
        return;
    for (;;) {
        if (depth == 0) {
            PartialSort(first, last, last, 0, order);
            return;
        }
        --depth;
        Tile* const middle = Step(first, Count(first, last) / 2);
        const Tile* const median =
            Cdecl<const Tile*>(addr::SC4RegionTileSort_Median, first, middle, Step(last, -1), order);
        const int32_t x = median->x;
        const int32_t y = median->y;
        const int32_t sideLog2 = median->sideLog2;
        Tile* const cut = Cdecl<Tile*>(addr::SC4RegionTileSort_Partition, first, last, x, y, sideLog2, order);
        IntroSort(cut, last, 0, depth, order);
        last = cut;
        if (Count(first, last) <= 0x10)
            return;
    }
}

// The game's 0x007AED00 (cdecl, three arguments). The STL sort over layout
// tiles: the main loop with twice the binary logarithm of the count as its
// depth allowance, then the insertion pass.
void __cdecl SortTiles(Tile* first, Tile* last, uint32_t order)
{
    if (first == last)
        return;
    int32_t count = Count(first, last);
    int32_t log = 0;
    while (count != 1) {
        count >>= 1;
        ++log;
    }
    IntroSort(first, last, 0, log + log, order);
    InsertionSort(first, last, order);
}

// ---------------------------------------------------------------------------
// Tile records.
// ---------------------------------------------------------------------------

// A counted pointer taking another's object, as inlined six times in the
// record's assignment.
void AssignCounted(void** target, void** source)
{
    if (source == target)
        return;
    void* const old = *target;
    void* const fresh = *source;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    *target = fresh;
    if (old)
        Release(old);
}

// The game's 0x007ADFA0 (thiscall, ret 4). A tile record takes another's
// content: the plain fields, the six counted pictures, the four dword
// vectors, the list and the vector of 12-byte items.
char* __fastcall AssignRecord(char* self, void*, char* other)
{
    At<uint32_t>(self, 0x00) = At<uint32_t>(other, 0x00);
    At<uint32_t>(self, 0x04) = At<uint32_t>(other, 0x04);
    At<uint32_t>(self, tile::kX) = At<uint32_t>(other, tile::kX);
    At<uint32_t>(self, tile::kY) = At<uint32_t>(other, tile::kY);
    At<uint32_t>(self, tile::kMapX) = At<uint32_t>(other, tile::kMapX);
    At<uint32_t>(self, tile::kMapY) = At<uint32_t>(other, tile::kMapY);
    At<uint8_t>(self, tile::kSideLog2) = At<uint8_t>(other, tile::kSideLog2);
    AssignCounted(&At<void*>(self, tile::kPicture), &At<void*>(other, tile::kPicture));
    AssignCounted(&At<void*>(self, tile::kMask), &At<void*>(other, tile::kMask));
    AssignCounted(&At<void*>(self, tile::kSecondPicture), &At<void*>(other, tile::kSecondPicture));
    AssignCounted(&At<void*>(self, tile::kSecondMask), &At<void*>(other, tile::kSecondMask));
    AssignCounted(&At<void*>(self, tile::kScratch), &At<void*>(other, tile::kScratch));
    AssignCounted(&At<void*>(self, tile::kExtraPicture), &At<void*>(other, tile::kExtraPicture));
    At<uint8_t>(self, tile::kFlag) = At<uint8_t>(other, tile::kFlag);
    This<void*>(addr::SC4Uint32Vector_CopyAssign, self + tile::kVector38, other + tile::kVector38);
    This<void*>(addr::SC4Uint32Vector_CopyAssign, self + tile::kSpansFirst, other + tile::kSpansFirst);
    This<void*>(addr::SC4Uint32Vector_CopyAssign, self + tile::kSpansThird, other + tile::kSpansThird);
    This<void*>(addr::SC4Uint32Vector_CopyAssign, self + tile::kSpansSecond, other + tile::kSpansSecond);
    At<uint32_t>(self, tile::kPointX) = At<uint32_t>(other, tile::kPointX);
    At<uint32_t>(self, tile::kPointY) = At<uint32_t>(other, tile::kPointY);
    This<void*>(addr::SC4RegionRefList_Assign, self + tile::kList, other + tile::kList);
    This<void*>(addr::SC4TrafficRecords_Update, self + tile::kVector74, other + tile::kVector74);
    return self;
}

// A counted pointer copied into raw memory.
void CopyCounted(char* self, char* other, std::size_t offset)
{
    void* const object = At<void*>(other, offset);
    At<void*>(self, offset) = object;
    if (object)
        AddRef(object);
}

// The game's 0x007AE7B0 (thiscall, ret 4). The tile record's copy
// constructor.
char* __fastcall CopyRecord(char* self, void*, char* other)
{
    At<uint32_t>(self, 0x00) = At<uint32_t>(other, 0x00);
    At<uint32_t>(self, 0x04) = At<uint32_t>(other, 0x04);
    At<uint32_t>(self, tile::kX) = At<uint32_t>(other, tile::kX);
    At<uint32_t>(self, tile::kY) = At<uint32_t>(other, tile::kY);
    At<uint32_t>(self, tile::kMapX) = At<uint32_t>(other, tile::kMapX);
    At<uint32_t>(self, tile::kMapY) = At<uint32_t>(other, tile::kMapY);
    At<uint8_t>(self, tile::kSideLog2) = At<uint8_t>(other, tile::kSideLog2);
    CopyCounted(self, other, tile::kPicture);
    CopyCounted(self, other, tile::kMask);
    CopyCounted(self, other, tile::kSecondPicture);
    CopyCounted(self, other, tile::kSecondMask);
    CopyCounted(self, other, tile::kScratch);
    CopyCounted(self, other, tile::kExtraPicture);
    At<uint8_t>(self, tile::kFlag) = At<uint8_t>(other, tile::kFlag);
    This<void*>(addr::SC4Uint32Vector_Copy, self + tile::kVector38, other + tile::kVector38);
    This<void*>(addr::SC4Uint32Vector_Copy, self + tile::kSpansFirst, other + tile::kSpansFirst);
    This<void*>(addr::SC4Uint32Vector_Copy, self + tile::kSpansThird, other + tile::kSpansThird);
    This<void*>(addr::SC4Uint32Vector_Copy, self + tile::kSpansSecond, other + tile::kSpansSecond);
    At<uint32_t>(self, tile::kPointX) = At<uint32_t>(other, tile::kPointX);
    At<uint32_t>(self, tile::kPointY) = At<uint32_t>(other, tile::kPointY);

    At<ListNode*>(self, tile::kList) = nullptr;
    ListNode* const sentinel = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    sentinel->next = sentinel;
    sentinel->previous = sentinel;
    At<ListNode*>(self, tile::kList) = sentinel;
    ListNode* const otherSentinel = At<ListNode*>(other, tile::kList);
    ListNode* const otherFirst = otherSentinel->next;
    ListNode* const position = sentinel->next;
    char* tag = other;
    This<void>(addr::SC4RefList_InsertRange, self + tile::kList, position, otherFirst, otherSentinel, &tag);

    This<void*>(addr::SC4Vector12_CopyConstruct, self + tile::kVector74, other + tile::kVector74);
    return self;
}

// The game's 0x007AE9B0 (cdecl; the caller pushes two more values that are
// not read). Assigns the records of [first, last) to the records at `target`
// and returns the end of what was written.
char* __cdecl AssignRecords(char* first, char* last, char* target)
{
    int32_t count = static_cast<int32_t>(last - first) >> 7;
    if (count <= 0)
        return target;
    do {
        AssignRecord(target, nullptr, first);
        first += tile::kSize;
        target += tile::kSize;
    } while (--count != 0);
    return target;
}

// The game's 0x007AED60 (cdecl; the caller pushes one more value that is not
// read). Copy-constructs the records of [first, last) into raw memory at
// `target` and returns the end of what was made.
char* __cdecl CopyRecords(char* first, char* last, char* target)
{
    for (; first != last; first += tile::kSize, target += tile::kSize) {
        if (target)
            CopyRecord(target, nullptr, first);
    }
    return target;
}

// The game's 0x007AEDA0 (cdecl; the caller pushes one more value that is not
// read). Copy-constructs `count` records from one record into raw memory at
// `target` and returns the end of what was made.
char* __cdecl FillRecords(char* target, uint32_t count, char* value)
{
    if (count == 0)
        return target;
    do {
        if (target)
            CopyRecord(target, nullptr, value);
        target += tile::kSize;
    } while (--count != 0);
    return target;
}

// ---------------------------------------------------------------------------
// The pictures of a tile.
// ---------------------------------------------------------------------------

// The game's 0x007AE160 (cdecl, eight dwords and two doubles). Fills a 32-bit
// picture from another, shifted right and down by a fraction of a pixel:
// every target pixel is the weighted mean of four source pixels, pixels
// outside the source counting as 0. The weights are the tent
// (0x007AA0E0) at -shift and 1 - shift, scaled to sum 16384 (0x007AA860,
// 0x007BFF20). Two row buffers hold the last two source rows mixed across
// (0x007AA110); each target row mixes the two (0x007AABB0). Target row r
// takes source rows r - 1 and r.
void __cdecl ShiftPixels(char* target, int32_t targetStride, char* source, int32_t sourceStride, int32_t targetWidth,
                         int32_t targetHeight, int32_t sourceWidth, int32_t sourceHeight, double shiftX,
                         double shiftY)
{
    // The original keeps the two float weights in its first double argument's
    // place on the stack; the second double is read after that.
    float weights[2];
    struct {
        Uint32Vector older;
        Uint32Vector newer;
    } rows = {};
    int32_t across[2];
    int32_t down[2];
    int32_t row = -4;

    Cdecl<void>(addr::SC4RegionPictures_FillScaled, static_cast<float>(-shiftX), 1.0f, &weights[0], &weights[0] + 2,
                addr::SC4Image_TentFilter, 16384.0f);
    Cdecl<void>(addr::SC4Image_WeightsToFixed, &weights[0], &weights[0] + 2, &across[0], int32_t{0x4000});
    Cdecl<void>(addr::SC4RegionPictures_FillScaled, static_cast<float>(-shiftY), 1.0f, &weights[0], &weights[0] + 2,
                addr::SC4Image_TentFilter, 16384.0f);
    Cdecl<void>(addr::SC4Image_WeightsToFixed, &weights[0], &weights[0] + 2, &down[0], int32_t{0x4000});

    int32_t position = static_cast<int32_t>(0xFFFF0000);
    if (targetHeight > 0) {
        int32_t left = targetHeight;
        do {
            const int32_t wanted = (position >> 16) + 2;
            int32_t count = wanted - row;
            if (!(count < 2))
                count = 2;
            row = wanted - count;
            if (count > 0) {
                uint32_t zero = 0;
                do {
                    const Uint32Vector swapped = rows.older;
                    rows.older = rows.newer;
                    rows.newer = swapped;
                    const uint32_t size = static_cast<uint32_t>(
                        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(rows.newer.end) -
                                             reinterpret_cast<std::uintptr_t>(rows.newer.begin)) >>
                        2);
                    if (static_cast<uint32_t>(targetWidth) < size) {
                        rows.newer.end = rows.newer.begin + targetWidth;
                    } else {
                        This<void>(addr::SC4Uint32Vector_InsertN, &rows.newer, rows.newer.end,
                                   static_cast<uint32_t>(targetWidth) - size, &zero);
                    }
                    uint32_t* const buffer = rows.newer.begin;
                    if (row < 0 || row > sourceHeight - 1) {
                        Cdecl<void>(addr::SC4_FillUint32, buffer, uint32_t{0}, targetWidth);
                    } else {
                        const int32_t lastRow = sourceHeight - 1;
                        int32_t clamped = row < lastRow ? row : lastRow;
                        if (!(clamped > 0))
                            clamped = 0;
                        Cdecl<void>(addr::SC4Image_MixRowAcross, buffer, source + clamped * sourceStride, targetWidth,
                                    sourceWidth, &across[0], 0xFFFF0000u);
                    }
                    ++row;
                } while (--count != 0);
            }
            Cdecl<void>(addr::SC4RegionPictures_BlendRows, target, &rows, targetWidth, &down[0]);
            target += targetStride;
            position += 0x10000;
        } while (--left != 0);
    }
    if (rows.newer.begin)
        Free(rows.newer.begin);
    if (rows.older.begin)
        Free(rows.older.begin);
}

// The game's 0x007AE3D0 (cdecl: picture, place for the new picture, two
// doubles). A new 32-bit picture two pixels wider and higher than the given
// one, holding it shifted by the two fractions. False when there is no
// picture or the new one cannot be sized (it is released then, and the
// caller's pointer left dangling); true otherwise, also when the factory
// makes nothing or a lock fails.
uint8_t __cdecl ShiftPicture(void* picture, void** out, double shiftX, double shiftY)
{
    int32_t areas[12];
    int32_t* const area = &areas[0];
    int32_t* const madeArea = &areas[4];

    if (!picture)
        return 0;
    void* const service = Cdecl<void*>(addr::SC4_Singleton913C1A);
    if (!Virtual<uint8_t>(service, 0xC, out))
        return 1;
    void* const made = *out;
    Virtual<void>(picture, image::kGetArea, area);
    Virtual<void>(made, image::kGetArea, madeArea);
    if (!Virtual<uint8_t>(made, image::kInit, area[2] - area[0] + 2, area[3] - area[1] + 2, uint32_t{9},
                          uint32_t{0x20})) {
        Release(*out);
        return 0;
    }
    if (!Virtual<uint8_t>(picture, image::kLock, uint32_t{0x8040}))
        return 1;
    if (Virtual<uint8_t>(made, image::kLock, uint32_t{0x8040})) {
        const int32_t height = Virtual<int32_t>(picture, image::kHeight);
        const int32_t width = Virtual<int32_t>(picture, image::kWidth);
        const int32_t madeHeight = Virtual<int32_t>(made, image::kHeight);
        const int32_t madeWidth = Virtual<int32_t>(made, image::kWidth);
        const int32_t stride = Virtual<int32_t>(picture, image::kStride);
        char* const bits = Virtual<char*>(picture, image::kBits);
        const int32_t madeStride = Virtual<int32_t>(made, image::kStride);
        char* const madeBits = Virtual<char*>(made, image::kBits);
        ShiftPixels(madeBits, madeStride, bits, stride, madeWidth, madeHeight, width, height, shiftX, shiftY);
        Virtual<uint8_t>(made, image::kUnlock, uint32_t{0x8040});
    }
    Virtual<uint8_t>(picture, image::kUnlock, uint32_t{0x8040});
    return 1;
}

// The game's 0x007AD400 takes the vector in ESI and three stack arguments
// the caller removes: (vector, picture, shift, flag, function).
__declspec(naked) void SpansThunk()
{
    __asm push esi
    __asm mov esi, dword ptr [esp + 8]
    __asm push dword ptr [esp + 20]
    __asm push dword ptr [esp + 20]
    __asm push dword ptr [esp + 20]
    __asm call dword ptr [esp + 36]
    __asm add esp, 12
    __asm pop esi
    __asm ret
}

void MakeSpans(char* vector, void* picture, uint32_t shift, uint32_t flag)
{
    using ThunkFn = void(__cdecl*)(char*, void*, uint32_t, uint32_t, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&SpansThunk)(vector, picture, shift, flag, addr::SC4RegionPictures_CollectRuns);
}

// The game's 0x007AA6A0 takes the picture in EAX and two stack arguments the
// caller removes: (picture, x out, y out, function).
__declspec(naked) void PointThunk()
{
    __asm mov eax, dword ptr [esp + 4]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm call dword ptr [esp + 24]
    __asm add esp, 8
    __asm ret
}

void FindPoint(void* picture, char* x, char* y)
{
    using ThunkFn = void(__cdecl*)(void*, char*, char*, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&PointThunk)(picture, x, y, addr::SC4RegionPictures_OpaqueCentre);
}

// The game's 0x007AE510 (thiscall on the region screen window, ret 4: a tile
// record). Makes the record's pictures ready for its place on the map: the
// picture and its mask (and the second pair when both are there) are replaced
// by copies shifted by one minus the fraction of the map position; a scratch
// picture of the new size is made with the window's image factory; the mask
// goes into the picture's top byte (0x007ABCD0); the three dword vectors and
// the point are worked out from the mask.
void __fastcall PreparePictures(char* self, void*, char* record)
{
    void* const picture = At<void*>(record, tile::kPicture);
    if (picture)
        AddRef(picture);
    void* const mask = At<void*>(record, tile::kMask);
    if (mask)
        AddRef(mask);

    const double x = static_cast<double>(At<float>(record, tile::kMapX));
    const double y = static_cast<double>(At<float>(record, tile::kMapY));
    const double shiftX = 1.0 - (x - Cdecl<double>(addr::CRT_floor, x));
    const double shiftY = 1.0 - (y - Cdecl<double>(addr::CRT_floor, y));

    Reset(&At<void*>(record, tile::kMask));
    ShiftPicture(mask, &At<void*>(record, tile::kMask), shiftX, shiftY);
    Reset(&At<void*>(record, tile::kPicture));
    ShiftPicture(picture, &At<void*>(record, tile::kPicture), shiftX, shiftY);

    void* const second = At<void*>(record, tile::kSecondPicture);
    if (second && At<void*>(record, tile::kSecondMask)) {
        AddRef(second);
        void* const secondMask = At<void*>(record, tile::kSecondMask);
        if (secondMask)
            AddRef(secondMask);
        Reset(&At<void*>(record, tile::kSecondPicture));
        ShiftPicture(second, &At<void*>(record, tile::kSecondPicture), shiftX, shiftY);
        Reset(&At<void*>(record, tile::kSecondMask));
        ShiftPicture(secondMask, &At<void*>(record, tile::kSecondMask), shiftX, shiftY);
        if (secondMask)
            Release(secondMask);
        Release(second);
    }

    void* const factory = At<void*>(self, window::kImageFactory);
    Reset(&At<void*>(record, tile::kScratch));
    if (Virtual<uint8_t>(factory, 0xC, &At<void*>(record, tile::kScratch))) {
        const int32_t* const area = Virtual<int32_t*>(At<void*>(record, tile::kPicture), image::kArea);
        void* const scratch = At<void*>(record, tile::kScratch);
        const int32_t height = area[3] - area[1];
        const int32_t width = area[2] - area[0];
        if (!Virtual<uint8_t>(scratch, image::kInit, width, height, uint32_t{9}, uint32_t{0x20}))
            Reset(&At<void*>(record, tile::kScratch));
    }

    Cdecl<void>(addr::SC4RegionPictures_MergeMask, At<void*>(record, tile::kPicture), At<void*>(record, tile::kMask));
    MakeSpans(record + tile::kSpansFirst, At<void*>(record, tile::kMask), 0, 0);
    MakeSpans(record + tile::kSpansSecond, At<void*>(record, tile::kMask), 8, 1);
    MakeSpans(record + tile::kSpansThird, At<void*>(record, tile::kMask), 0x10, 1);
    if (void* const secondNow = At<void*>(record, tile::kSecondPicture)) {
        if (void* const secondMaskNow = At<void*>(record, tile::kSecondMask))
            Cdecl<void>(addr::SC4RegionPictures_MergeMask, secondNow, secondMaskNow);
    }
    FindPoint(At<void*>(record, tile::kMask), record + tile::kPointX, record + tile::kPointY);

    if (mask)
        Release(mask);
    if (picture)
        Release(picture);
}

// ---------------------------------------------------------------------------
// The cities under the tiles.
// ---------------------------------------------------------------------------

void* Nation()
{
    return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
}

// The game's 0x007AEA00 (thiscall on the region screen window, which it does
// not use, ret 8: a tile record and a byte). The open region's holder of the
// city at the record's place. When there is none and the byte is set, a new
// regional city of the tile's size is made there, named with the text
// 0x6A231EAA / 0xAA738C4E ('New City' when the text is missing), and put into
// the region. The path built from the regions folder and the region's folder
// is not used.
void* __fastcall CityAtTile(char* /*self*/, void*, char* record, uint8_t make)
{
    void* const nation = Nation();
    void* const region = Virtual<void*>(nation, 0x20);
    void* holder = Virtual<void*>(region, 0x2C, At<int32_t>(record, tile::kX), At<int32_t>(record, tile::kY));
    if (holder)
        return holder;
    if (!make)
        return holder;

    GameString path;
    This<void*>(addr::SC4String_FromIGZString, &path, Virtual<void*>(nation, 0x14));
    This<void>(addr::cRZBaseString_AppendString, &path, Virtual<void*>(region, 0x14));
    char* end = path.end;
    if (end + 1 == path.capacity) {
        const uint32_t size = static_cast<uint32_t>(end - path.begin);
        const uint32_t more = size < 1 ? 1 : size;
        This<void>(addr::cSC4Advisor_F40F1B0, &path.begin, more + size);
        end = path.end;
    }
    end[1] = 0;
    *path.end = '\\';
    path.end = path.end + 1;

    void* city = nullptr;
    if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1A0})) {
        city = This<void*>(addr::cSC4RegionalCity_Construct, memory);
        if (city)
            AddRef(city);
    }
    AddRef(city);
    Virtual<uint8_t>(city, 0xC);
    const uint32_t side = uint32_t{1} << (At<uint8_t>(record, tile::kSideLog2) & 31);
    Virtual<void>(city, 0x20, side, side);
    Virtual<void>(city, 0x18, At<int32_t>(record, tile::kX), At<int32_t>(record, tile::kY), uint32_t{0});

    GameString fallback;
    void* const built = This<void*>(addr::SC4String_FromText, &fallback, "New City");
    void* const fallbackBuffer = built ? static_cast<char*>(built) + 4 : nullptr;
    GameString name;
    char tag = 0;
    This<void>(addr::SC4String_CopyBuffer, &name.begin, fallbackBuffer, uint32_t{0}, 0xFFFFFFFFu, &tag);
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    DestroyString(fallback);
    Cdecl<void>(addr::SC4_LoadLocalizedText, &name, 0x6A231EAAu, 0xAA738C4Eu, uint32_t{0});
    Virtual<void>(city, 0x88, &name);
    Virtual<void>(region, 0x30, city);
    Release(city);
    holder = Virtual<void*>(region, 0x2C, At<int32_t>(record, tile::kX), At<int32_t>(record, tile::kY));
    DestroyString(name);
    Release(city);
    DestroyString(path);
    return holder;
}

// The game's 0x007AEC00 (thiscall on the region screen window, no
// arguments). Adds up the population of the cities under the window's tiles
// and shows the number in the control 0xC9E41918.
void __fastcall ShowPopulation(char* self, void*)
{
    char* const end = At<char*>(self, window::kTilesEnd);
    int32_t total = 0;
    for (char* record = At<char*>(self, window::kTilesBegin); record != end; record += tile::kSize) {
        void* const region = Virtual<void*>(Nation(), 0x20);
        void** const holder =
            Virtual<void**>(region, 0x2C, At<int32_t>(record, tile::kX), At<int32_t>(record, tile::kY));
        if (holder)
            total += Virtual<int32_t>(*holder, 0x28);
    }

    void* const formatter = Virtual<void*>(Cdecl<void*>(addr::SC4_CurrentApp), 0x98, uint32_t{0});
    GameString text;
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
    Virtual<void>(formatter, 0x48, static_cast<int64_t>(total), &text);
    void* const control = Virtual<void*>(self, window::kChildByID, window::kPopulationText);
    Virtual<void>(control, 0x128, &text);
    DestroyString(text);
}

bool InstallScreenPart4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4RegionTileSort_PartialSort, reinterpret_cast<void*>(&PartialSort)},
        {addr::SC4RegionTileSort_FinalInsertionSort, reinterpret_cast<void*>(&InsertionSort)},
        {addr::SC4RegionTileRecord_Assign, reinterpret_cast<void*>(&AssignRecord)},
        {addr::SC4RegionPictures_ShiftPixels, reinterpret_cast<void*>(&ShiftPixels)},
        {addr::SC4RegionPictures_ShiftPicture, reinterpret_cast<void*>(&ShiftPicture)},
        {addr::cSC4WinRegionScreen_PrepareTilePictures, reinterpret_cast<void*>(&PreparePictures)},
        {addr::SC4RegionTileRecord_CopyConstruct, reinterpret_cast<void*>(&CopyRecord)},
        {addr::SC4RegionTileSort_IntroSortLoop, reinterpret_cast<void*>(&IntroSort)},
        {addr::SC4RegionTileRecord_AssignRange, reinterpret_cast<void*>(&AssignRecords)},
        {addr::cSC4WinRegionScreen_CityAtTile, reinterpret_cast<void*>(&CityAtTile)},
        {addr::cSC4WinRegionScreen_ShowPopulation, reinterpret_cast<void*>(&ShowPopulation)},
        {addr::SC4RegionTileSort_Sort, reinterpret_cast<void*>(&SortTiles)},
        {addr::SC4RegionTileRecord_UninitializedCopy, reinterpret_cast<void*>(&CopyRecords)},
        {addr::SC4RegionTileRecord_UninitializedFillN, reinterpret_cast<void*>(&FillRecords)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static void* originals[count] = {};
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_screenPart4Registered = registry::Register({
    "region.screen.part4",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_PrepareTilePictures,
    "Our region screen tiles: tile records, shifted tile pictures, the city under a tile, the population total "
    "and the ordering of layout tiles (docs/region.md)",
    false,
    InstallScreenPart4,
});

} // namespace
} // namespace opensc4::region
