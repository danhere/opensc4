// The region screen's code, first part, 0x007AA2A0 to 0x007AB630: the small
// classes the region screen window (cSC4WinRegionScreen) is built from, and
// free helpers of its later parts.
//
//   - the flat terrain map (0x28 bytes; main vtable 0x00AB8C00 with the 40
//     slots of cISTETerrainMap, interface 0xAB953253; the reference count
//     part at +4 has the vtable 0x00AB8BE8). The screen makes one for the
//     open region (0x007ACD75), keeps it at screen +0x16C and publishes it at
//     0x00B43CF8: a terrain that is 270 high everywhere, with sea level 0 and
//     cells 64 wide;
//   - the scroll window (0x138 bytes, a window, vtable 0x00AB8CD0; the screen
//     keeps it at +0x174): turns the mouse into scrolling of the region map.
//     While the right button is held the offset from where it went down
//     gives a speed and one of nine cursors; with the option on, the pointer
//     at an edge of the window scrolls too; on every paint it sends its
//     target what to scroll by, and lets the movement die out afterwards;
//   - the view window (0xDC bytes, a window, vtable 0x00AB8F50; screen +0xE8):
//     holds the game's 3D view (0x00B43DD0) and draws it when painted;
//   - two functions of the screen itself (QueryInterface and the slot that
//     passes the pointer's place to the mouse move slot) and the deleting
//     destructor of the screen's terrain overlay (a small counted object,
//     vtables 0x00AB8CB8 and 0x00AB8CA0) whose other code is in the second
//     part;
//   - helpers: the ordering of region layout tiles {x, y, log2 of the side}
//     and the sort pieces made for it, the centre of a picture's opaque
//     pixels, a table filled from a function and scaled to a sum, a blend of
//     two pixel rows, and the callback that makes a window the target of
//     every button under another.
//
// Replacement, off by default:
//
//   region.screen.part1: everything above.
//
// Left the game's: 0x007AA300 (cell width; four bytes, shared with another
// class's vtable), 0x007AA4D0 and 0x007AA500 (four bytes each: too short to
// hook), 0x007AA8E0 (SC4::CellPairsDiffer, shared).
//
// The window base class, the window manager, cursors, the view and the
// stopwatch are the game's, called through their vtables or by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::region {
namespace {

namespace terrain { // the flat terrain map
constexpr std::size_t kCount = 0x04;     // the reference count part (vtable, count)
constexpr std::size_t kAltitude = 0x0C;  // float, 270
constexpr std::size_t kSeaLevel = 0x10;  // float, 0
constexpr std::size_t kCellsX = 0x14;
constexpr std::size_t kCellsZ = 0x18;
constexpr std::size_t kRowLength = 0x1C; // vertices in a row: cells across + 1
constexpr std::size_t kCellWidth = 0x20; // float, 64
constexpr std::size_t kReady = 0x24;     // byte
} // namespace terrain

namespace win { // the window base class
constexpr std::size_t kManager = 0x04;
constexpr std::size_t kTarget = 0x4C;  // the window messages are sent to
constexpr std::size_t kDrawing = 0x6C; // what the paint slot brackets its drawing with
} // namespace win

namespace scroll { // the scroll window
constexpr std::size_t kCursor = 0xD8;    // the ID of the cursor it last set
constexpr std::size_t kAnchorX = 0xDC;   // where the right button went down
constexpr std::size_t kAnchorY = 0xE0;
constexpr std::size_t kSpeedX = 0xE4;    // float: the speed of the last tick, dying out
constexpr std::size_t kSpeedY = 0xE8;
constexpr std::size_t kSlowX = 0xEC;     // float: what the speed loses in a second
constexpr std::size_t kSlowY = 0xF0;
constexpr std::size_t kDragX = 0xF4;     // float: the speed the held button asks for
constexpr std::size_t kDragY = 0xF8;
constexpr std::size_t kOtherX = 0xFC;    // float: a second speed, set by the later parts
constexpr std::size_t kOtherY = 0x100;
constexpr std::size_t kEdgeSpeed = 0x104; // float, 300
constexpr std::size_t kMoving = 0x108;   // byte: the start message is out
constexpr std::size_t kDragging = 0x109; // byte
constexpr std::size_t kHeld = 0x10C;     // a counted object, released at Shutdown
constexpr std::size_t kInner = 0x110;    // 10: offsets shorter than this do nothing
constexpr std::size_t kOuter = 0x114;    // 0x20: offsets are cut to this
constexpr std::size_t kSlowRate = 0x118; // float, 5
constexpr std::size_t kDivisor = 0x11C;  // float, 3
constexpr std::size_t kTimer = 0x120;    // a stopwatch, 0x18 bytes
} // namespace scroll

namespace view { // the view window
constexpr std::size_t kView = 0xD8;
} // namespace view

namespace screen {
constexpr std::size_t kFilter = 0xD8; // the message filter part
} // namespace screen

constexpr uint32_t kTerrainMapIid = 0xAB953253;
constexpr uint32_t kWinIid = 0x22BA0121; // also the ID the target callback is registered under
constexpr uint32_t kFilterIid = 0xC6AE7085;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kOptionGroupIid = 0xA1336CC0;

constexpr uint32_t kPlainCursor = 0xC2A676AC;
constexpr uint32_t kDragMarker = 0x48E945B4; // a child window the button-down slot makes
constexpr uint32_t kScrollStart = 0x8A4BAC53;
constexpr uint32_t kScrollBy = 0xAAA1CDF2;
constexpr uint32_t kScrollEnd = 0x8A4BAC5B;

// Constants the original reads from its data.
constexpr float kMaxAllowedHeight = 1023.0f; // 0x00AB8B80
constexpr float kRoundBias = 12582912.0f;    // 0x00AB8BE0: 1.5 * 2^23
constexpr float kHalf = 0.5f;                // 0x00A84D2C
constexpr float kMillisecond = 0.001f;       // 0x00A867A4
constexpr float kFixedPoint = 256.0f;        // 0x00A9422C

// A tile of the region's layout.
struct Tile {
    int32_t x;
    int32_t y;
    int32_t size; // log2 of the side
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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// --- The flat terrain map ------------------------------------------------------

// The game's 0x007AA2A0, slot 0: QueryInterface for 1 and 0xAB953253.
uint8_t __fastcall TerrainQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kTerrainMapIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return 0;
}

// The game's 0x007AA2E0, slot 3: Init.
uint8_t __fastcall TerrainInit(char* self, void*)
{
    At<uint8_t>(self, terrain::kReady) = 1;
    return 1;
}

// The game's 0x007AA2F0, slot 4: Shutdown.
uint8_t __fastcall TerrainShutdown(char* self, void*)
{
    At<uint8_t>(self, terrain::kReady) = 0;
    return 1;
}

// The game's 0x007AA310, slot 9: the index of the vertex (x, z).
int32_t __fastcall TerrainVertexIndex(char* self, void*, int32_t x, int32_t z)
{
    return static_cast<int32_t>(At<uint32_t>(self, terrain::kRowLength) * static_cast<uint32_t>(z) +
                                static_cast<uint32_t>(x));
}

// The game's 0x007AA320, slot 10: the vertex of an index.
void __fastcall TerrainVertexOf(char* self, void*, int32_t index, int32_t* x, int32_t* z)
{
    *x = index % At<int32_t>(self, terrain::kRowLength);
    *z = index / At<int32_t>(self, terrain::kRowLength);
}

// The game's 0x007AA350, slot 11: whether (x, z) is on the map. Returns a
// whole word, 1 or 0.
uint32_t __fastcall TerrainInBounds(char* self, void*, float x, float z)
{
    if (!(x >= 0.0f))
        return 0;
    if (!(z >= 0.0f))
        return 0;
    const double scale = 1.0 / static_cast<double>(At<float>(self, terrain::kCellWidth));
    if (!(static_cast<double>(At<int32_t>(self, terrain::kCellsX)) > static_cast<double>(x) * scale))
        return 0;
    if (!(static_cast<double>(At<int32_t>(self, terrain::kCellsZ)) > static_cast<double>(z) * scale))
        return 0;
    return 1;
}

// The game's 0x007AA3B0, slot 12: the altitude at a vertex index.
double __fastcall TerrainAltitudeAtIndex(char* self, void*, int32_t)
{
    return static_cast<double>(At<float>(self, terrain::kAltitude));
}

// The game's 0x007AA4B0, slots 13, 14, 18 and 19: the altitude at a place, at
// a vertex, the highest surface of a cell and the altitude at the nearest
// vertex. Two arguments of four bytes each, floats or integers, not read.
double __fastcall TerrainAltitudeAt(char* self, void*, uint32_t, uint32_t)
{
    return static_cast<double>(At<float>(self, terrain::kAltitude));
}

// The game's 0x007AA3C0, slot 15: the altitudes of a rectangle of vertices
// {left, top, right, bottom} into a table whose rows are rowStride floats
// apart and whose entries are columnStride floats apart. Columns run to
// right inclusive, rows stop before bottom.
void __fastcall TerrainAltitudes(char* self, void*, const int32_t* area, char* out, int32_t rowStride,
                                 int32_t columnStride)
{
    int32_t row = area[1];
    if (row >= area[3])
        return;
    const uint32_t rowBytes = static_cast<uint32_t>(rowStride) * 4;
    do {
        char* cell = out;
        out += static_cast<int32_t>(rowBytes);
        int32_t column = area[0];
        if (column <= area[2]) {
            const int32_t columnBytes = static_cast<int32_t>(static_cast<uint32_t>(columnStride) << 2);
            do {
                *reinterpret_cast<uint32_t*>(cell) = At<uint32_t>(self, terrain::kAltitude);
                cell += columnBytes;
                ++column;
            } while (column <= area[2]);
        }
        ++row;
    } while (row < area[3]);
}

// The game's 0x007AA430, slot 16: the water depths of a rectangle, as the
// altitudes above: all 0.
void __fastcall TerrainWaterDepths(char*, void*, const int32_t* area, char* out, int32_t rowStride,
                                   int32_t columnStride)
{
    int32_t row = area[1];
    if (row >= area[3])
        return;
    const int32_t rowBytes = static_cast<int32_t>(static_cast<uint32_t>(rowStride) << 2);
    do {
        char* cell = out;
        out += rowBytes;
        int32_t column = area[0];
        if (column <= area[2]) {
            const int32_t columnBytes = static_cast<int32_t>(static_cast<uint32_t>(columnStride) << 2);
            do {
                *reinterpret_cast<uint32_t*>(cell) = 0;
                cell += columnBytes;
                ++column;
            } while (column <= area[2]);
        }
        ++row;
    } while (row < area[3]);
}

// The game's 0x007AA490, slot 17: the altitudes of a cell's four corners.
void __fastcall TerrainCornerAltitudes(char* self, void*, uint32_t, uint32_t, float* out)
{
    const float altitude = At<float>(self, terrain::kAltitude);
    out[3] = altitude;
    out[2] = altitude;
    out[1] = altitude;
    out[0] = altitude;
}

// The game's 0x007AA4C0, slots 20 and 31: the altitude checksum of a
// rectangle and the water level at a vertex index, both 0.
double __fastcall TerrainZeroOne(char*, void*, uint32_t)
{
    return static_cast<double>(Global<float>(addr::SC4_FloatZero));
}

// The game's 0x007AA4E0, slot 23: the lowest height allowed, 0.
double __fastcall TerrainMinAllowedHeight(char*, void*)
{
    return static_cast<double>(Global<float>(addr::SC4_FloatZero));
}

// The game's 0x007AA4F0, slot 24: the highest height allowed, 1023.
double __fastcall TerrainMaxAllowedHeight(char*, void*)
{
    return static_cast<double>(kMaxAllowedHeight);
}

// The game's 0x007AA510, slots 30, 32 and 33: the water level at a place,
// the smoothed water level and the water depth at a vertex, all 0.
double __fastcall TerrainZeroTwo(char*, void*, uint32_t, uint32_t)
{
    return static_cast<double>(Global<float>(addr::SC4_FloatZero));
}

// The whole number below a coordinate as 0x007AAD60 gets it: the value less
// a half is rounded to a whole number by adding 1.5 * 2^23 as a float, the
// float's bits give the number, and a value that is itself the next whole
// number moves up to it.
int32_t CellOf(double value)
{
    const float biased = static_cast<float>((value - static_cast<double>(kHalf)) + static_cast<double>(kRoundBias));
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    const uint32_t below = bits + 0xB4C00000u;
    const uint32_t next = below + 1;
    if (value == static_cast<double>(static_cast<int32_t>(next)))
        return static_cast<int32_t>(next);
    return static_cast<int32_t>(below);
}

// The game's 0x007AAD60, slot 25: the index of the grid vertex at or before
// (x, z).
int32_t __fastcall TerrainGridIndex(char* self, void*, float x, float z)
{
    const double scale = 1.0 / static_cast<double>(At<float>(self, terrain::kCellWidth));
    const uint32_t row = static_cast<uint32_t>(CellOf(static_cast<double>(z) * scale));
    const uint32_t column = static_cast<uint32_t>(CellOf(scale * static_cast<double>(x)));
    return static_cast<int32_t>(At<uint32_t>(self, terrain::kRowLength) * row + column);
}

// The game's 0x007AADF0, slots 26 to 28: the normal at a place, smoothed or
// at a vertex: straight up.
void __fastcall TerrainNormal(char*, void*, uint32_t, uint32_t, float* out)
{
    out[0] = 0.0f;
    out[1] = 1.0f;
    out[2] = 0.0f;
}

// The game's 0x007AACE0: the constructor (cells across, cells down).
void* __fastcall TerrainConstruct(char* self, void*, int32_t cellsX, int32_t cellsZ)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionTerrainMap_BaseVtable;
    This<void*>(addr::cRZBaseRefCount_Construct, self + terrain::kCount);
    At<int32_t>(self, terrain::kCellsX) = cellsX;
    At<std::uintptr_t>(self, terrain::kCount) = addr::SC4RegionTerrainMap_CountVtable;
    At<int32_t>(self, terrain::kRowLength) = static_cast<int32_t>(static_cast<uint32_t>(cellsX) + 1);
    At<std::uintptr_t>(self, 0) = addr::SC4RegionTerrainMap_Vtable;
    At<float>(self, terrain::kAltitude) = 270.0f;
    At<uint32_t>(self, terrain::kSeaLevel) = 0;
    At<int32_t>(self, terrain::kCellsZ) = cellsZ;
    At<float>(self, terrain::kCellWidth) = 64.0f;
    At<uint8_t>(self, terrain::kReady) = 0;
    return self;
}

// The game's 0x007AAD30: the deleting destructor.
void* __fastcall TerrainDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionTerrainMap_Vtable;
    At<std::uintptr_t>(self, terrain::kCount) = addr::SC4RegionTerrainMap_CountVtable;
    This<void>(addr::cRZBaseRefCount_Destruct, self + terrain::kCount);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007AAAF0, slot 0 of the count part's vtable: the deleting
// destructor from the part at +4.
void* __fastcall TerrainDeleteFromCount(char* part, void*, uint32_t flags)
{
    return TerrainDelete(part - terrain::kCount, nullptr, flags);
}

// --- The scroll window ---------------------------------------------------------

// The game's 0x007AA520, slot 4: Init. The plain cursor, and flags 0x10000
// and 0x8000 off.
uint8_t __fastcall ScrollInit(char* self, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        This<uint8_t>(addr::GZWin_Init, self);
        At<uint32_t>(self, scroll::kCursor) = kPlainCursor;
        Virtual<void>(self, 0x110, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(self, 0x110, uint32_t{0x8000}, uint32_t{0});
    }
    return 1;
}

// The game's 0x007AA570, slot 5: Shutdown.
uint8_t __fastcall ScrollShutdown(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        if (void* const held = At<void*>(self, scroll::kHeld)) {
            Release(held);
            At<void*>(self, scroll::kHeld) = nullptr;
        }
        This<uint8_t>(addr::SC4Win_F99D2FE, self);
    }
    return 1;
}

// The game's 0x007AAE10: the constructor.
void* __fastcall ScrollConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4RegionScroller_Vtable;
    At<uint32_t>(self, scroll::kAnchorX) = 0;
    At<uint32_t>(self, scroll::kAnchorY) = 0;
    At<uint32_t>(self, scroll::kSpeedX) = 0;
    At<uint32_t>(self, scroll::kSpeedY) = 0;
    At<uint32_t>(self, scroll::kSlowX) = 0;
    At<uint32_t>(self, scroll::kSlowY) = 0;
    At<uint32_t>(self, scroll::kDragX) = 0;
    At<uint32_t>(self, scroll::kDragY) = 0;
    At<uint32_t>(self, scroll::kOtherX) = 0;
    At<uint32_t>(self, scroll::kOtherY) = 0;
    At<float>(self, scroll::kEdgeSpeed) = 300.0f;
    At<uint8_t>(self, scroll::kMoving) = 0;
    At<uint8_t>(self, scroll::kDragging) = 0;
    At<void*>(self, scroll::kHeld) = nullptr;
    At<uint32_t>(self, scroll::kInner) = 10;
    At<uint32_t>(self, scroll::kOuter) = 0x20;
    At<float>(self, scroll::kSlowRate) = 5.0f;
    At<float>(self, scroll::kDivisor) = 3.0f;
    This<void*>(addr::SC4Stopwatch_Construct, self + scroll::kTimer, uint32_t{4});
    return self;
}

// Gives the window the cursor of an ID when the cursor service has it, as
// the original does at 0x007AB061 and 0x007AB0F0.
void GiveCursor(char* self, uint32_t id)
{
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const cursors = Virtual<void*>(manager, 0x80);
    if (!cursors)
        return;
    void* const cursor = Virtual<void*>(cursors, 0x24, id);
    if (!cursor)
        return;
    At<uint32_t>(self, scroll::kCursor) = id;
    This<void>(addr::GZWin_SetCursorPointer, self, cursor, uint32_t{1});
}

// The game's 0x007AAEC0, slot 137 (three arguments, not read; the right
// button coming up, going by its neighbours): removes the marker child,
// stops the drag, gives the mouse back and sets flag 0x200000.
uint8_t __fastcall ScrollButtonUp(char* self, void*, uint32_t, uint32_t, uint32_t)
{
    if (void* const marker = Virtual<void*>(self, 0x88, kDragMarker))
        Virtual<void>(self, 0x40, marker);
    At<uint8_t>(self, scroll::kDragging) = 0;
    At<uint32_t>(self, scroll::kDragX) = 0;
    At<uint32_t>(self, scroll::kDragY) = 0;
    Virtual<void>(At<void*>(self, win::kManager), 0x78, static_cast<void*>(self));
    Virtual<void>(self, 0x110, uint32_t{0x200000}, uint32_t{1});
    return 1;
}

// The game's 0x007AAF20 (it runs to 0x007AB126), slot 138: the mouse moved
// to (x, y). The offset from the anchor gives the drag speed: nothing inside
// the inner radius; outside it the direction, times a third of what the
// offset (cut to the outer radius) has over the inner radius when that is 1
// or more. The cursor is the plain one inside, else one of nine by the
// offset's place against the inner radius on each axis. Returns 0.
uint8_t __fastcall ScrollMouseMove(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(x) - At<uint32_t>(self, scroll::kAnchorX));
    const float across = static_cast<float>(static_cast<double>(dx));
    const double down = static_cast<double>(
        static_cast<int32_t>(static_cast<uint32_t>(y) - At<uint32_t>(self, scroll::kAnchorY)));
    const uint32_t inner = At<uint32_t>(self, scroll::kInner);
    const double square = down * down + static_cast<double>(across) * static_cast<double>(across);
    const double innerSquare = static_cast<double>(inner * inner);

    uint32_t cursorId;
    if (square < innerSquare) {
        At<uint32_t>(self, scroll::kDragX) = 0;
        At<uint32_t>(self, scroll::kDragY) = 0;
        cursorId = kPlainCursor;
        GiveCursor(self, cursorId);
    } else {
        const double length = std::sqrt(square);
        const float outer = static_cast<float>(static_cast<double>(At<uint32_t>(self, scroll::kOuter)));
        double reach = length;
        if (length > static_cast<double>(outer))
            reach = static_cast<double>(outer);
        const double over =
            (reach - static_cast<double>(inner)) / static_cast<double>(At<float>(self, scroll::kDivisor));
        const float overRounded = static_cast<float>(over);
        const float factor = over < 1.0 ? 1.0f : overRounded;
        const float unit = static_cast<float>(static_cast<double>(factor) / length);
        const float dragX = static_cast<float>(static_cast<double>(unit) * static_cast<double>(across));
        const float dragY = static_cast<float>(down * static_cast<double>(unit));
        At<float>(self, scroll::kDragX) = dragX;
        At<float>(self, scroll::kDragY) = dragY;

        const int32_t radius = At<int32_t>(self, scroll::kInner);
        const int32_t negated = static_cast<int32_t>(0u - static_cast<uint32_t>(radius));
        const float low = static_cast<float>(static_cast<double>(negated));
        const double high = static_cast<double>(radius);
        uint32_t column = static_cast<double>(across) < static_cast<double>(low) ? 0u : 1u;
        column += static_cast<double>(across) < high ? 0u : 1u;
        uint32_t line = down < static_cast<double>(low) ? 0u : 1u;
        line += down < high ? 0u : 1u;
        cursorId = reinterpret_cast<const uint32_t*>(addr::SC4RegionScroller_CursorIds)[column + line * 3];
    }
    GiveCursor(self, cursorId);
    return 0;
}

// -1, 0 or 1 by a float's sign, as the original compares it with 0.
int32_t SignOf(float value)
{
    if (value < 0.0f)
        return -1;
    return value > 0.0f ? 1 : 0;
}

// The game's 0x007AB130 (it runs to 0x007AB519), slot 88, called when the
// window is painted: one tick of scrolling over the time since the last one.
// The speed is the drag speed plus the second speed, plus or minus the edge
// speed when the edge option (preferences +0xF09) is on, the graphics
// service says yes and the pointer is within a pixel of an edge. A moving
// tick sends the target window a start message once and then how far to
// scroll, in 1/256ths; a still tick lets the last speed die out at the rate
// set when it was last moving and sends the end message once.
uint8_t __fastcall ScrollTick(char* self, void*)
{
    char* const timer = self + scroll::kTimer;
    if (!This<uint8_t>(addr::GZStopwatch_IsRunning, timer)) {
        This<void>(addr::SC4Stopwatch_Start, timer);
        return 1;
    }
    const int32_t elapsed = This<int32_t>(addr::GZStopwatch_ElapsedMilliseconds, timer);
    This<void>(addr::GZStopwatch_Start, timer);

    // The two speeds live in two stack slots that the still branch reuses.
    float first =
        static_cast<float>(static_cast<double>(At<float>(self, scroll::kOtherX)) +
                           static_cast<double>(At<float>(self, scroll::kDragX)));
    float second =
        static_cast<float>(static_cast<double>(At<float>(self, scroll::kDragY)) +
                           static_cast<double>(At<float>(self, scroll::kOtherY)));

    // The original's two slots for the pointer's place hold 1 and the
    // released service pointer before the window manager fills them.
    int32_t pointerX = 0;
    int32_t pointerY = 0;
    bool edges = false;
    if (At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0xF09)) {
        void* service = nullptr;
        if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
            Virtual<uint8_t>(framework, 0x14, uint32_t{0xC416025C}, uint32_t{0x0073283C}, &service);
        edges = Virtual<uint8_t>(service, 0x30) != 0;
        pointerX = 1;
        pointerY = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(service));
        This<void>(addr::SC4RefPtr_Destruct, &service);
    }
    if (edges) {
        Virtual<void>(At<void*>(self, win::kManager), 0x88, static_cast<void*>(self), &pointerX, &pointerY);
        const double edgeSpeed = static_cast<double>(At<float>(self, scroll::kEdgeSpeed));
        if (pointerX <= static_cast<int32_t>(Virtual<uint32_t>(self, 0xAC) + 1))
            first = static_cast<float>(static_cast<double>(first) - edgeSpeed);
        else if (pointerX >= static_cast<int32_t>(Virtual<uint32_t>(self, 0xB4) - 1))
            first = static_cast<float>(static_cast<double>(first) + edgeSpeed);
        if (pointerY <= static_cast<int32_t>(Virtual<uint32_t>(self, 0xB0) + 1))
            second = static_cast<float>(static_cast<double>(second) - edgeSpeed);
        else if (pointerY >= static_cast<int32_t>(Virtual<uint32_t>(self, 0xB8) - 1))
            second = static_cast<float>(static_cast<double>(second) + edgeSpeed);
    }

    if (first == 0.0f && second == 0.0f) {
        const double seconds = static_cast<double>(elapsed) * static_cast<double>(kMillisecond);
        first = At<float>(self, scroll::kSpeedX);
        second = At<float>(self, scroll::kSpeedY);
        const double lostX = seconds * static_cast<double>(At<float>(self, scroll::kSlowX));
        const float lostY = static_cast<float>(seconds * static_cast<double>(At<float>(self, scroll::kSlowY)));
        At<float>(self, scroll::kSpeedX) =
            static_cast<float>(static_cast<double>(At<float>(self, scroll::kSpeedX)) - lostX);
        At<float>(self, scroll::kSpeedY) =
            static_cast<float>(static_cast<double>(At<float>(self, scroll::kSpeedY)) - static_cast<double>(lostY));
        const int32_t wasX = SignOf(first);
        const int32_t wasY = SignOf(second);
        if (SignOf(At<float>(self, scroll::kSpeedX)) != wasX || SignOf(At<float>(self, scroll::kSpeedY)) != wasY) {
            At<uint32_t>(self, scroll::kSlowY) = 0;
            At<uint32_t>(self, scroll::kSlowX) = 0;
            At<uint32_t>(self, scroll::kSpeedX) = 0;
            At<uint32_t>(self, scroll::kSpeedY) = At<uint32_t>(self, scroll::kSlowY);
        }
    } else {
        At<float>(self, scroll::kSpeedX) = first;
        At<float>(self, scroll::kSpeedY) = second;
        const float rate = At<float>(self, scroll::kSlowRate);
        const float slowX = static_cast<float>(static_cast<double>(rate) * static_cast<double>(first));
        const float slowY = static_cast<float>(static_cast<double>(second) * static_cast<double>(rate));
        At<float>(self, scroll::kSlowX) = slowX;
        At<float>(self, scroll::kSlowY) = slowY;
    }

    if (first == 0.0f && second == 0.0f) {
        if (At<uint8_t>(self, scroll::kMoving)) {
            Virtual<void>(self, 0x248, At<void*>(self, win::kTarget), kScrollEnd, uint32_t{0}, uint32_t{0},
                          uint32_t{0});
            At<uint8_t>(self, scroll::kMoving) = 0;
        }
        return 1;
    }

    const double seconds = static_cast<double>(elapsed) * static_cast<double>(kMillisecond);
    const bool moving = At<uint8_t>(self, scroll::kMoving) != 0;
    first = static_cast<float>(static_cast<double>(first) * seconds);
    second = static_cast<float>(static_cast<double>(second) * seconds);
    if (!moving) {
        void* const target = At<void*>(self, win::kTarget);
        At<uint8_t>(self, scroll::kMoving) = 1;
        Virtual<void>(self, 0x248, target, kScrollStart, uint32_t{0}, uint32_t{0}, uint32_t{0});
    }
    const int32_t byY = Truncate(static_cast<double>(second) * static_cast<double>(kFixedPoint));
    const int32_t byX = Truncate(static_cast<double>(first) * static_cast<double>(kFixedPoint));
    Virtual<void>(self, 0x248, At<void*>(self, win::kTarget), kScrollBy, byX, byY, uint32_t{0});
    return 1;
}

// --- The view window -----------------------------------------------------------

// The game's 0x007AB520, slot 4: Init. Takes the game's 3D view, then the
// window base class's Init.
uint32_t __fastcall ViewInit(char* self, void*)
{
    void* const now = Global<void*>(addr::SC4_View43DD0);
    void* const old = At<void*>(self, view::kView);
    if (now != old) {
        if (now)
            AddRef(now);
        At<void*>(self, view::kView) = now;
        if (old)
            Release(old);
    }
    return This<uint32_t>(addr::GZWin_Init, self);
}

// The game's 0x007AB560, slot 5: Shutdown. Lets the view go, then the window
// base class's.
uint32_t __fastcall ViewShutdown(char* self, void*)
{
    if (void* const held = At<void*>(self, view::kView)) {
        At<void*>(self, view::kView) = nullptr;
        Release(held);
    }
    return This<uint32_t>(addr::SC4Win_F99D2FE, self);
}

// The game's 0x007AB590, slot 88, called when the window is painted: the
// view's slot 0x54 between slots 0xA8 and 0xAC of the window's +0x6C.
uint8_t __fastcall ViewPaint(char* self, void*)
{
    Virtual<void>(At<void*>(self, win::kDrawing), 0xA8);
    Virtual<void>(At<void*>(self, view::kView), 0x54);
    Virtual<void>(At<void*>(self, win::kDrawing), 0xAC);
    return 1;
}

// The game's 0x007AB5E0: the destructor.
void __fastcall ViewDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionScreen_BackWin_Vtable;
    if (void* const held = At<void*>(self, view::kView))
        Release(held);
    This<void>(addr::GZWin_Destruct, self);
}

// The game's 0x007AB5C0, slot 148: the deleting destructor.
void* __fastcall ViewDelete(char* self, void*, uint32_t flags)
{
    ViewDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- The screen and the overlay ------------------------------------------------

// The game's 0x007AA640, slot 0 of the region screen window (the two thunks
// at 0x007B0E20 and 0x007B0E30 jump to it): QueryInterface for 1, the window
// interface and the message filter interface, which is the part at +0xD8.
uint8_t __fastcall ScreenQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == kWinIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kFilterIid) {
        *out = self ? self + screen::kFilter : nullptr;
        AddRef(self);
        return 1;
    }
    return 0;
}

// The game's 0x007AA600, slot 141 of the region screen window (one argument,
// not read): asks the window manager where the pointer is and passes that to
// the screen's mouse move slot with no flags. The two numbers are not set
// before the manager is asked, as in the original.
uint8_t __fastcall ScreenPointerCame(char* self, void*, uint32_t)
{
    int32_t x;
    int32_t y;
    Virtual<void>(At<void*>(self, win::kManager), 0x88, static_cast<void*>(self), &x, &y);
    Virtual<void>(self, 0x228, x, y, uint32_t{0});
    return 1;
}

// The game's 0x007AB600: the deleting destructor of the terrain overlay
// (vtables 0x00AB8CB8 and 0x00AB8CA0; made at 0x007AC7D0).
void* __fastcall OverlayDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4RegionOverlay_Vtable;
    At<std::uintptr_t>(self, 4) = addr::SC4RegionOverlay_UnknownVtable;
    This<void>(addr::cRZBaseUnknown_Destruct, self + 4);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007AAB00, slot 3 of the overlay's second vtable: the deleting
// destructor from the part at +4.
void* __fastcall OverlayDeleteFromPart(char* part, void*, uint32_t flags)
{
    return OverlayDelete(part - 4, nullptr, flags);
}

// --- Helpers -------------------------------------------------------------------

// What tiles are ordered by: 2^size + y + x, in 32 bits.
uint32_t TileKey(int32_t x, int32_t y, int32_t size)
{
    return (1u << (static_cast<uint32_t>(size) & 31)) + static_cast<uint32_t>(y) + static_cast<uint32_t>(x);
}

// Whether the first tile comes before the second: by the key (the sign of
// the difference), then by x.
bool Before(int32_t x, int32_t y, int32_t size, int32_t otherX, int32_t otherY, int32_t otherSize)
{
    const int32_t difference = static_cast<int32_t>(TileKey(x, y, size) - TileKey(otherX, otherY, otherSize));
    if (difference < 0)
        return true;
    return difference == 0 && x < otherX;
}

bool Before(const Tile* a, const Tile* b)
{
    return Before(a->x, a->y, a->size, b->x, b->y, b->size);
}

// The game's 0x007AA5A0: the tile ordering as a function object's call
// operator (`this` is an empty object, not read). Returns a whole word.
uint32_t __fastcall TileBefore(void*, void*, const Tile* a, const Tile* b)
{
    return Before(a, b) ? 1 : 0;
}

// The game's 0x007AA920 (cdecl; a fourth argument, the empty ordering
// object, is not read): the middle one of three tiles.
const Tile* __cdecl TileMedian(const Tile* a, const Tile* b, const Tile* c)
{
    if (Before(a, b)) {
        if (Before(b, c))
            return b;
        if (Before(a, c))
            return c;
        return a;
    }
    if (Before(a, c))
        return a;
    if (Before(b, c))
        return c;
    return b;
}

// The game's 0x007AA9C0 (cdecl: a place in a sorted run and a tile by
// value; the ordering object after them is not read): moves the tiles
// before the place up by one while the new tile comes before them and puts
// it in the gap. It does not look for the start of the run: the caller
// knows a tile that stops it.
void __cdecl TileInsertBack(Tile* gap, int32_t x, int32_t y, int32_t size)
{
    Tile* previous = gap - 1;
    while (Before(x, y, size, previous->x, previous->y, previous->size)) {
        gap->x = previous->x;
        gap->y = previous->y;
        gap->size = previous->size;
        gap = previous;
        --previous;
    }
    gap->x = x;
    gap->y = y;
    gap->size = size;
}

// The game's 0x007AAA40 (cdecl: the heap, the index of a gap, the top
// index and a tile by value; the ordering object after them is not read):
// moves the gap up a heap whose parents come after their children, and puts
// the tile in it.
void __cdecl TileSiftUp(Tile* heap, int32_t gap, int32_t top, int32_t x, int32_t y, int32_t size)
{
    int32_t parent = (gap - 1) / 2;
    while (gap > top) {
        const Tile* const above = heap + parent;
        if (!Before(above->x, above->y, above->size, x, y, size))
            break;
        Tile* const here = heap + gap;
        here->x = above->x;
        here->y = above->y;
        here->size = above->size;
        gap = parent;
        parent = (parent - 1) / 2;
    }
    Tile* const here = heap + gap;
    here->x = x;
    here->y = y;
    here->size = size;
}

// The game's 0x007AA6A0: the picture in EAX, two results on the stack (the
// caller removes them). The centre of the picture's opaque pixels: the mean
// place of its 16-bit pixels weighted by each one's high byte, rounded; the
// middle of the picture when it cannot be locked or nothing is opaque.
void __cdecl OpaqueCentre(void* picture, int32_t* outX, int32_t* outY)
{
    const int32_t* const area = Virtual<const int32_t*>(picture, 0x30);
    const int32_t width = area[2];
    const int32_t height = area[3];
    int64_t sumX = 0;
    int64_t sumY = 0;
    int64_t weight = 0;
    if (Virtual<uint8_t>(picture, 0x18, uint32_t{0x40})) {
        for (int32_t row = 0; row < height; ++row) {
            const uint32_t stride = Virtual<uint32_t>(picture, 0x8C);
            const uint8_t* const line = Virtual<const uint8_t*>(picture, 0x88) +
                                        static_cast<int32_t>(stride * static_cast<uint32_t>(row));
            uint32_t rowX = 0;
            uint32_t rowY = 0;
            uint32_t rowWeight = 0;
            for (int32_t column = 0; column < width; ++column) {
                const uint32_t opacity = line[column * 2 + 1];
                if (opacity) {
                    rowX += opacity * static_cast<uint32_t>(column);
                    rowY += opacity * static_cast<uint32_t>(row);
                    rowWeight += opacity;
                }
            }
            sumX += static_cast<int32_t>(rowX);
            sumY += static_cast<int32_t>(rowY);
            weight += static_cast<int32_t>(rowWeight);
        }
        Virtual<void>(picture, 0x1C, uint32_t{0x40});
        if (weight > 0) {
            const int64_t half = weight / 2;
            *outX = static_cast<int32_t>((sumX + half) / weight);
            *outY = static_cast<int32_t>((sumY + half) / weight);
            return;
        }
    }
    *outX = width / 2;
    *outY = height / 2;
}

__declspec(naked) void OpaqueCentreThunk()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push eax
    __asm call OpaqueCentre
    __asm add esp, 12
    __asm ret
}

// The game's 0x007AA860 (cdecl): fills [begin, end) with shape(x) for x
// from start in steps, then scales the table so that it adds up to total.
// The sum is kept as a float but takes each value as the function returned
// it.
void __cdecl FillScaled(float start, float step, float* begin, float* end, double(__cdecl* shape)(double),
                        float total)
{
    float sum = 0.0f;
    if (begin != end) {
        float* entry = begin;
        do {
            const double value = shape(static_cast<double>(start));
            *entry = static_cast<float>(value);
            ++entry;
            sum = static_cast<float>(value + static_cast<double>(sum));
            start = static_cast<float>(static_cast<double>(start) + static_cast<double>(step));
        } while (entry != end);
    }
    const float scale = static_cast<float>(static_cast<double>(total) / static_cast<double>(sum));
    if (begin != end) {
        float* entry = begin;
        do {
            *entry = static_cast<float>(static_cast<double>(scale) * static_cast<double>(*entry));
            ++entry;
        } while (entry != end);
    }
}

// One channel of the blend below: rounded, 14 bits dropped, kept in a byte.
uint32_t BlendChannel(uint32_t sum)
{
    int32_t value = static_cast<int32_t>(sum + 0x2000) >> 14;
    if (static_cast<uint32_t>(value) >= 0x100)
        value = static_cast<uint8_t>(~(value >> 31));
    return static_cast<uint32_t>(value);
}

// The game's 0x007AABB0 (cdecl): count pixels of four bytes, each the sum of
// a pixel of the first row (sources +0) times weights[0] and one of the
// second row (sources +0xC) times weights[1], in 2.14 fixed point.
void __cdecl BlendRows(uint32_t* out, char* sources, int32_t count, const int32_t* weights)
{
    if (count <= 0)
        return;
    int32_t index = 0;
    do {
        const uint32_t a = At<const uint32_t*>(sources, 0)[index];
        const uint32_t weightA = static_cast<uint32_t>(weights[0]);
        uint32_t third = ((a >> 16) & 0xFF) * weightA;
        uint32_t second = ((a >> 8) & 0xFF) * weightA;
        uint32_t first = (a & 0xFF) * weightA;
        uint32_t fourth = (a >> 24) * weightA;
        const uint32_t b = At<const uint32_t*>(sources, 0xC)[index];
        const uint32_t weightB = static_cast<uint32_t>(weights[1]);
        fourth += (b >> 24) * weightB;
        third += ((b >> 16) & 0xFF) * weightB;
        first += (b & 0xFF) * weightB;
        second += ((b >> 8) & 0xFF) * weightB;
        const uint32_t top = BlendChannel(fourth);
        const uint32_t high = BlendChannel(third);
        const uint32_t middle = BlendChannel(second);
        const uint32_t low = BlendChannel(first);
        *out = (((((top << 8) + high) << 8) + middle) << 8) + low;
        ++out;
        ++index;
    } while (index < count);
}

// The game's 0x007AAB10 (cdecl), a child enumeration callback (parent, child
// ID, child, context): clears the child's flag 0x8000; a button gets the
// context as its target; an option group gets it too, and under any other
// window the callback goes on through its children.
uint8_t __cdecl TargetCallback(void*, uint32_t, void* child, void* context)
{
    Virtual<void>(child, 0x110, uint32_t{0x8000}, uint32_t{0});
    void* button;
    if (Virtual<uint8_t>(child, 0x0, kButtonIid, &button)) {
        Virtual<void>(child, 0x158, context);
        Release(button);
    }
    void* group = nullptr;
    if (Virtual<uint8_t>(child, 0x0, kOptionGroupIid, &group))
        Virtual<void>(child, 0x158, context);
    else
        Virtual<void>(child, 0x80, kWinIid, reinterpret_cast<void*>(addr::cSC4WinRegionScreen_ButtonTargetCallback), context);
    if (group)
        Release(group);
    return 1;
}

bool InstallRegionScreen1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4RegionTerrainMap_QueryInterface, reinterpret_cast<void*>(&TerrainQueryInterface)},
        {addr::SC4RegionTerrainMap_Init, reinterpret_cast<void*>(&TerrainInit)},
        {addr::SC4RegionTerrainMap_Shutdown, reinterpret_cast<void*>(&TerrainShutdown)},
        {addr::SC4RegionTerrainMap_GetVertIndex, reinterpret_cast<void*>(&TerrainVertexIndex)},
        {addr::SC4RegionTerrainMap_GetVertex, reinterpret_cast<void*>(&TerrainVertexOf)},
        {addr::SC4RegionTerrainMap_LocationIsInBounds, reinterpret_cast<void*>(&TerrainInBounds)},
        {addr::SC4RegionTerrainMap_GetAltitudeAtIndex, reinterpret_cast<void*>(&TerrainAltitudeAtIndex)},
        {addr::SC4RegionTerrainMap_GetAltitudes, reinterpret_cast<void*>(&TerrainAltitudes)},
        {addr::SC4RegionTerrainMap_GetWaterDepths, reinterpret_cast<void*>(&TerrainWaterDepths)},
        {addr::SC4RegionTerrainMap_GetCellCornerAltitudes, reinterpret_cast<void*>(&TerrainCornerAltitudes)},
        {addr::SC4RegionTerrainMap_GetAltitude, reinterpret_cast<void*>(&TerrainAltitudeAt)},
        {addr::SC4RegionTerrainMap_ZeroForIndex, reinterpret_cast<void*>(&TerrainZeroOne)},
        {addr::SC4RegionTerrainMap_MinAllowedHeight, reinterpret_cast<void*>(&TerrainMinAllowedHeight)},
        {addr::SC4RegionTerrainMap_MaxAllowedHeight, reinterpret_cast<void*>(&TerrainMaxAllowedHeight)},
        {addr::SC4RegionTerrainMap_ZeroForCell, reinterpret_cast<void*>(&TerrainZeroTwo)},
        {addr::SC4RegionScroller_Init, reinterpret_cast<void*>(&ScrollInit)},
        {addr::SC4RegionScroller_Shutdown, reinterpret_cast<void*>(&ScrollShutdown)},
        {addr::SC4RegionTileSort_Less, reinterpret_cast<void*>(&TileBefore)},
        {addr::cSC4WinRegionScreen_OnMouseEnter, reinterpret_cast<void*>(&ScreenPointerCame)},
        {addr::cSC4WinRegionScreen_QueryInterface, reinterpret_cast<void*>(&ScreenQueryInterface)},
        {addr::SC4RegionPictures_OpaqueCentre, reinterpret_cast<void*>(&OpaqueCentreThunk)},
        {addr::SC4RegionPictures_FillScaled, reinterpret_cast<void*>(&FillScaled)},
        {addr::SC4RegionTileSort_Median, reinterpret_cast<void*>(&TileMedian)},
        {addr::SC4RegionTileSort_InsertBackward, reinterpret_cast<void*>(&TileInsertBack)},
        {addr::SC4RegionTileSort_PushHeap, reinterpret_cast<void*>(&TileSiftUp)},
        {addr::SC4RegionTerrainMap_DeletingDestructorThunk, reinterpret_cast<void*>(&TerrainDeleteFromCount)},
        {addr::SC4RegionOverlay_DeletingDestructorThunk, reinterpret_cast<void*>(&OverlayDeleteFromPart)},
        {addr::cSC4WinRegionScreen_ButtonTargetCallback, reinterpret_cast<void*>(&TargetCallback)},
        {addr::SC4RegionPictures_BlendRows, reinterpret_cast<void*>(&BlendRows)},
        {addr::SC4RegionTerrainMap_Construct, reinterpret_cast<void*>(&TerrainConstruct)},
        {addr::SC4RegionTerrainMap_DeletingDestructor, reinterpret_cast<void*>(&TerrainDelete)},
        {addr::SC4RegionTerrainMap_GetTerrainGridIndex, reinterpret_cast<void*>(&TerrainGridIndex)},
        {addr::SC4RegionTerrainMap_GetNormal, reinterpret_cast<void*>(&TerrainNormal)},
        {addr::SC4RegionScroller_Construct, reinterpret_cast<void*>(&ScrollConstruct)},
        {addr::SC4RegionScroller_OnMouseUpR, reinterpret_cast<void*>(&ScrollButtonUp)},
        {addr::SC4RegionScroller_OnMouseMove, reinterpret_cast<void*>(&ScrollMouseMove)},
        {addr::SC4RegionScroller_Tick, reinterpret_cast<void*>(&ScrollTick)},
        {addr::SC4RegionBackWin_Init, reinterpret_cast<void*>(&ViewInit)},
        {addr::SC4RegionBackWin_Shutdown, reinterpret_cast<void*>(&ViewShutdown)},
        {addr::SC4RegionBackWin_Paint, reinterpret_cast<void*>(&ViewPaint)},
        {addr::SC4RegionBackWin_DeletingDestructor, reinterpret_cast<void*>(&ViewDelete)},
        {addr::SC4RegionBackWin_Destruct, reinterpret_cast<void*>(&ViewDestruct)},
        {addr::SC4RegionOverlay_DeletingDestructor, reinterpret_cast<void*>(&OverlayDelete)},
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

const bool g_regionScreen1Registered = registry::Register({
    "region.screen.part1",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RegionTerrainMap_QueryInterface,
    "Our region screen pieces, part 1: the flat terrain map, the scroll and view windows, tile and picture helpers",
    false,
    InstallRegionScreen1,
});

} // namespace
} // namespace opensc4::region
