// The region map window (docs/region.md), part 1: 0x007B2440 to 0x007B3300,
// the start of cSC4WinRegionView's code. Replacement, off by default:
//
//   region.view.part1: the small functions of the map window (main vtable
//     0x00AB9658, 151 slots) and of the three things it owns:
//
//     - the window itself: the point test of slot 149 (0x007B2440), the
//       redraw request (0x007B29E0), the three setters the region screen
//       calls (names and borders flags, the map mode), a tile's place in the
//       window (0x007B3030) and its rectangle on the map (0x007B3110), and the
//       check that blanks the name of a tutorial city (0x007B3170);
//     - the picture grid (vtables 0x00AB9630 and 0x00AB9618 at +4, interface
//       0xC989F960, kept at window +0x10C): the map is cut into cells, each
//       with its own picture and a 'stale' byte. 0x007B2620 marks the cells
//       under a rectangle stale, 0x007B2770 has the stale ones composed again
//       (by slot 3 of the window's part at +0xD8, 0x007B4150) and copies all
//       of them to a picture, and slot 3 (0x007B28B0) is what the 3D view
//       calls to put the map on the screen. The three copy loops over cells
//       (0x007B31D0, 0x007B3220, 0x007B3290) are the cell vector's;
//     - the two pixel loops the tile composer uses: 0x007B2A30 lays a tile's
//       picture over the cell by the runs of its mask, and 0x007B2DD0 tints
//       the runs of a mask with a colour (the border and the pick marks);
//     - the window procedure (vtable 0x00AB95F0, 0x007B2500) that bobs a child
//       window up and down on a 1.5 second sine.
//
// Still the game's: 0x007B2480 and 0x007B24B0 (a counted pointer to the
// framework's service 0xC2C2EB0F that about sixty functions of the window
// classes share).
//
// The picture buffers, the window base class, the 3D view and the driver are
// the game's, called through their vtables. 0x007B2A30 and 0x007B2DD0 take
// arguments in registers and sit behind small assembly entries.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace view { // cSC4WinRegionView, as far as this part shows it
constexpr std::size_t kViewX = 0xE8;    // the view's place on the map, pixels
constexpr std::size_t kViewY = 0xEC;
constexpr std::size_t kTiles = 0x100;   // vector of tile record pointers
constexpr std::size_t kTilesEnd = 0x104;
constexpr std::size_t kGrid = 0x10C;    // the picture grid
constexpr std::size_t kNames = 0x113;   // show the city names
constexpr std::size_t kFlag114 = 0x114; // two flags set together
constexpr std::size_t kFlag115 = 0x115;
constexpr std::size_t kMode = 0x118;    // the map mode
} // namespace view

namespace grid { // the picture grid (vtable 0x00AB9630)
constexpr std::size_t kCells = 0x0C;    // vector of cells, row by row
constexpr std::size_t kCellsEnd = 0x10;
constexpr std::size_t kFirstX = 0x18;   // the first cell's place, in cells
constexpr std::size_t kFirstY = 0x1C;
constexpr std::size_t kShiftX = 0x20;   // pixels of the first cell off the view's corner
constexpr std::size_t kShiftY = 0x24;
constexpr std::size_t kCellWidth = 0x28;
constexpr std::size_t kCellHeight = 0x2C;
constexpr std::size_t kColumns = 0x30;
constexpr std::size_t kRows = 0x34;
constexpr std::size_t kWidth = 0x38;    // the view's size
constexpr std::size_t kHeight = 0x3C;
constexpr std::size_t kComposer = 0x40; // slot 3 (picture, x, y) composes a cell
} // namespace grid

namespace tile { // a tile record of the region screen
constexpr std::size_t kMapX = 0x10; // floats
constexpr std::size_t kMapY = 0x14;
constexpr std::size_t kPicture = 0x1C;
constexpr std::size_t kComposed = 0x34;
} // namespace tile

namespace flash { // the bobbing window procedure (vtable 0x00AB95F0)
constexpr std::size_t kStart = 0x0C;  // milliseconds
constexpr std::size_t kWindow = 0x10; // the child window it moves
} // namespace flash

namespace image { // cIGZBuffer, by vtable offset
constexpr std::size_t kLock = 0x18;     // (flags)
constexpr std::size_t kUnlock = 0x1C;   // (flags)
constexpr std::size_t kHeight = 0x28;
constexpr std::size_t kArea = 0x30;     // -> int32_t[4]: left, top, right, bottom
constexpr std::size_t kGetPixel = 0x54; // (x, y)
constexpr std::size_t kSetPixel = 0x58; // (x, y, colour)
constexpr std::size_t kCopy = 0x74;     // (source, source area, target area, clip or 0)
constexpr std::size_t kColour = 0x78;   // (red, green, blue)
constexpr std::size_t kToRgb = 0x7C;    // (colour, uint8_t* red, green, blue)
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace image

namespace win { // the window base class, by vtable offset
constexpr std::size_t kParent = 0x2C;
constexpr std::size_t kChild = 0x8C;      // (id)
constexpr std::size_t kHeight = 0xA8;
constexpr std::size_t kMoveTo = 0xE0;     // (x, y)
constexpr std::size_t kConvert = 0xF0;    // (int32_t* x, int32_t* y)
} // namespace win

constexpr uint32_t kFlashStart = 0xA2BF8AD5;
constexpr uint32_t kFlashStop = 0xA2BF8AD6;
constexpr uint32_t kFlashTick = 0xA2BF8ACD;
constexpr uint32_t kFlashChild = 0x4A630000;
constexpr uint32_t kFlashPeriod = 1500; // milliseconds

// A cell of the picture grid.
struct Cell {
    void* picture;
    uint8_t stale;
    uint8_t pad[3];
};
static_assert(sizeof(Cell) == 0x8);

struct DwordVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

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

// The game's inlined reference holder: take another object.
void Assign(void*& holder, void* fresh)
{
    void* const old = holder;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    holder = fresh;
    if (old)
        Release(old);
}

// 32-bit sums and differences that wrap as the game's do.
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

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

double X87Sin(double x)
{
    double result;
    __asm {
        fld x
        fsin
        fstp result
    }
    return result;
}

// The 3D view (the global at 0x00B43DD0): slot 0x50 asks for a redraw, 0x68
// and 0x60 bracket the grid's drawing.
void* View()
{
    return Global<void*>(addr::SC4_View43DD0);
}

// The game's inlined division that rounds down.
int32_t FloorDivide(int32_t value, int32_t by)
{
    int32_t quotient = value / by;
    if (value < 0 && Mul(quotient, by) != value)
        --quotient;
    return quotient;
}

// The game's 0x007B2440, slot 149 of the main vtable (x, y): converts the
// point (slot 0xF0) and answers 1 when no child window is under it.
uint32_t __fastcall PointIsOwn(char* self, void*, int32_t x, int32_t y)
{
    Virtual<void>(self, win::kConvert, &x, &y);
    return Cdecl<void*>(addr::SC4UI_F78D6E0, static_cast<void*>(self), x, y) == nullptr ? 1u : 0u;
}

// The game's 0x007B2500, slot 3 of the vtable 0x00AB95F0 (window, message):
// the procedure of the bobbing child window. 0xA2BF8AD5 starts it (the clock
// and the parent's child 0x4A630000 are kept), 0xA2BF8AD6 stops it, and on
// 0xA2BF8ACD the child is moved to y = (parent height - own height) times
// (1 - |sin|) of the time's place in a 1.5 second turn. The tick is answered
// with 0.
uint8_t __fastcall FlashProc(char* self, void*, void* window, uint32_t* message)
{
    const uint32_t type = message[0];
    if (type == kFlashTick) {
        if (At<void*>(self, flash::kWindow)) {
            const uint32_t now = Cdecl<uint32_t>(addr::RZ_TimeMilliseconds);
            const uint32_t phase = (now - At<uint32_t>(self, flash::kStart)) % kFlashPeriod;
            const double angle = static_cast<double>(phase) * Global<double>(addr::SC4RegionView_FlashPerMillisecond) *
                                 Global<double>(addr::SC4RegionView_FlashTurn);
            const double wave = 1.0 - std::fabs(X87Sin(angle));
            void* const parent = Virtual<void*>(At<void*>(self, flash::kWindow), win::kParent);
            void* const child = At<void*>(self, flash::kWindow);
            const int32_t whole = Virtual<int32_t>(parent, win::kHeight);
            const int32_t own = Virtual<int32_t>(child, win::kHeight);
            const int32_t y = Truncate(static_cast<double>(Sub(whole, own)) * wave);
            Virtual<void>(child, win::kMoveTo, int32_t{0}, y);
        }
        return 0;
    }
    if (type == kFlashStart) {
        At<uint32_t>(self, flash::kStart) = Cdecl<uint32_t>(addr::RZ_TimeMilliseconds);
        void* const child = Virtual<void*>(window, win::kChild, kFlashChild);
        Assign(At<void*>(self, flash::kWindow), child);
        return 1;
    }
    if (type == kFlashStop) {
        if (void* const child = At<void*>(self, flash::kWindow)) {
            At<void*>(self, flash::kWindow) = nullptr;
            Release(child);
        }
        return 1;
    }
    return 0;
}

// The game's 0x007B2620 (int32_t rect[4]), of the picture grid: marks the
// cells under the rectangle (map pixels) stale. The first cell is not below
// 0, the last not past the grid.
void __fastcall GridMarkStale(char* self, void*, const int32_t* rect)
{
    const int32_t originX = Mul(At<int32_t>(self, grid::kFirstX), At<int32_t>(self, grid::kCellWidth));
    const int32_t originY = Mul(At<int32_t>(self, grid::kFirstY), At<int32_t>(self, grid::kCellHeight));

    int32_t firstX = FloorDivide(Sub(rect[0], originX), At<int32_t>(self, grid::kCellWidth));
    if (!(firstX > 0))
        firstX = 0;
    int32_t firstY = FloorDivide(Sub(rect[1], originY), At<int32_t>(self, grid::kCellHeight));
    if (!(firstY > 0))
        firstY = 0;

    int32_t lastX = FloorDivide(Sub(Sub(rect[2], originX), 1), At<int32_t>(self, grid::kCellWidth));
    const int32_t limitX = Sub(At<int32_t>(self, grid::kColumns), 1);
    if (!(lastX < limitX))
        lastX = limitX;
    int32_t lastY = FloorDivide(Sub(Sub(rect[3], originY), 1), At<int32_t>(self, grid::kCellHeight));
    const int32_t limitY = Sub(At<int32_t>(self, grid::kRows), 1);
    if (!(lastY < limitY))
        lastY = limitY;

    for (int32_t y = firstY; y <= lastY; ++y) {
        for (int32_t x = firstX; x <= lastX; ++x) {
            const int32_t index = Add(Mul(At<int32_t>(self, grid::kColumns), y), x);
            At<Cell*>(self, grid::kCells)[index].stale = 1;
        }
    }
}

// The game's 0x007B2770 (picture, x, y), of the picture grid: copies every
// cell's picture to `target` at its place (the view's rectangle moved by x, y
// is the clip), after having the stale ones composed again by the composer's
// slot 3 (cell picture, the cell's x and y in map pixels). Returns 0 when the
// target cannot be locked.
uint8_t __fastcall GridPaint(char* self, void*, void* target, int32_t x, int32_t y)
{
    int32_t clip[4];
    clip[0] = x;
    clip[1] = y;
    clip[2] = Add(At<int32_t>(self, grid::kWidth), x);
    clip[3] = Add(At<int32_t>(self, grid::kHeight), y);
    if (!Virtual<uint8_t>(target, image::kLock, uint32_t{0x8010}))
        return 0;

    int32_t index = 0;
    for (int32_t row = 0; row < At<int32_t>(self, grid::kRows); ++row) {
        for (int32_t column = 0; column < At<int32_t>(self, grid::kColumns); ++column) {
            Cell* const cell = At<Cell*>(self, grid::kCells) + index;
            ++index;
            const int32_t cellWidth = At<int32_t>(self, grid::kCellWidth);
            const int32_t cellHeight = At<int32_t>(self, grid::kCellHeight);
            int32_t place[4];
            place[0] = Sub(Mul(cellWidth, column), At<int32_t>(self, grid::kShiftX));
            place[1] = Sub(Mul(cellHeight, row), At<int32_t>(self, grid::kShiftY));
            place[2] = Add(cellWidth, place[0]);
            place[3] = Add(cellHeight, place[1]);
            if (cell->stale) {
                const int32_t mapY =
                    Mul(Add(At<int32_t>(self, grid::kFirstY), row), At<int32_t>(self, grid::kCellHeight));
                const int32_t mapX =
                    Mul(Add(At<int32_t>(self, grid::kFirstX), column), At<int32_t>(self, grid::kCellWidth));
                Virtual<void>(At<void*>(self, grid::kComposer), 0xC, cell->picture, mapX, mapY);
                cell->stale = 0;
            }
            void* const picture = cell->picture;
            const int32_t* const area = Virtual<const int32_t*>(picture, image::kArea);
            Virtual<void>(target, image::kCopy, picture, area, &place[0], &clip[0]);
        }
    }
    Virtual<void>(target, image::kUnlock, uint32_t{0x8010});
    return 1;
}

// The game's 0x007B29E0: everything is stale. Marks every cell of the grid
// and clears the 'composed' byte of every tile record, then asks the 3D view
// for a redraw (a jump to its slot 0x50, whose result is returned).
uint32_t __fastcall Invalidate(char* self, void*)
{
    char* const pictures = At<char*>(self, view::kGrid);
    Cell* const cellsEnd = At<Cell*>(pictures, grid::kCellsEnd);
    for (Cell* cell = At<Cell*>(pictures, grid::kCells); cell != cellsEnd; ++cell)
        cell->stale = 1;

    char** const tilesEnd = At<char**>(self, view::kTilesEnd);
    for (char** each = At<char**>(self, view::kTiles); each != tilesEnd; ++each)
        At<uint8_t>(*each, tile::kComposed) = 0;

    return Virtual<uint32_t>(View(), 0x50);
}

// The game's 0x007B2A30 (in ECX the tile picture's own rectangle, in EAX its
// rectangle on the target, in EDI the target; on the stack the tile picture
// and the vector of its mask's runs, cleaned by the caller). Lays the tile
// picture over the target: both rectangles are cut to the picture and to the
// target, and each run (y * 0x10000 + first x, the x after the last) is
// copied whole when its first pixel is opaque, else pixel by pixel as
// target * (255 - alpha) / 256 + source, each channel held at 255.
void __cdecl BlendRuns(const int32_t* sourcePlace, const int32_t* targetPlace, void* target, void* source,
                       DwordVector* runs)
{
    const int32_t* const sourceArea = Virtual<const int32_t*>(source, image::kArea);
    const int32_t sourceLeft = sourceArea[0];
    const int32_t sourceTop = sourceArea[1];
    const int32_t sourceRight = sourceArea[2];
    const int32_t sourceBottom = sourceArea[3];
    const int32_t* const targetArea = Virtual<const int32_t*>(target, image::kArea);
    const int32_t targetLeft = targetArea[0];
    const int32_t targetTop = targetArea[1];
    const int32_t targetRight = targetArea[2];
    const int32_t targetBottom = targetArea[3];

    int32_t fromLeft = sourcePlace[0];
    int32_t fromTop = sourcePlace[1];
    int32_t fromRight = sourcePlace[2];
    int32_t fromBottom = sourcePlace[3];
    int32_t toLeft = targetPlace[0];
    int32_t toTop = targetPlace[1];
    int32_t toRight = targetPlace[2];
    int32_t toBottom = targetPlace[3];

    int32_t cut = Sub(sourceLeft, fromLeft);
    if (cut > 0) {
        fromLeft = Add(fromLeft, cut);
        toLeft = Add(toLeft, cut);
    }
    cut = Sub(sourceTop, fromTop);
    if (cut > 0) {
        fromTop = Add(fromTop, cut);
        toTop = Add(toTop, cut);
    }
    cut = Sub(sourceRight, fromRight);
    if (cut < 0) {
        fromRight = Add(fromRight, cut);
        toRight = Add(toRight, cut);
    }
    cut = Sub(sourceBottom, fromBottom);
    if (cut < 0) {
        fromBottom = Add(fromBottom, cut);
        toBottom = Add(toBottom, cut);
    }
    cut = Sub(targetLeft, toLeft);
    if (cut > 0) {
        fromLeft = Add(fromLeft, cut);
        toLeft = Add(toLeft, cut);
    }
    cut = Sub(targetTop, toTop);
    if (cut > 0) {
        fromTop = Add(fromTop, cut);
        toTop = Add(toTop, cut);
    }
    cut = Sub(targetRight, toRight);
    if (cut < 0) {
        fromRight = Add(fromRight, cut);
        toRight = Add(toRight, cut);
    }
    cut = Sub(targetBottom, toBottom);
    if (cut < 0) {
        fromBottom = Add(fromBottom, cut);
        toBottom = Add(toBottom, cut);
    }
    if (Sub(toRight, toLeft) <= 0)
        return;
    if (Sub(toBottom, toTop) <= 0)
        return;

    if (!Virtual<uint8_t>(target, image::kLock, uint32_t{0x8080}))
        return;
    if (Virtual<uint8_t>(source, image::kLock, uint32_t{0x40})) {
        const uint32_t* run = runs->begin;
        const uint32_t* const runsEnd = runs->end;
        while (run != runsEnd) {
            const uint32_t first = run[0];
            const int32_t row = static_cast<int32_t>(first >> 16);
            const int32_t start = static_cast<int32_t>(first & 0xFFFF);
            const int32_t from = start < fromLeft ? fromLeft : start;
            const int32_t stop = static_cast<int32_t>(run[1] & 0xFFFF);
            const int32_t to = fromRight < stop ? fromRight : stop;
            run += 2;
            if (row < fromTop || row >= fromBottom || to <= from)
                continue;

            const int32_t stride = Virtual<int32_t>(source, image::kStride);
            const uint32_t rowOffset = static_cast<uint32_t>(Mul(stride, row));
            const uint32_t bits = Virtual<uint32_t>(source, image::kBits);
            const uint32_t* const pixels =
                reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(rowOffset + bits));
            const int32_t y = Add(Sub(row, fromTop), toTop);
            const int32_t shift = Sub(toLeft, fromLeft);

            if (pixels[from] >= 0xFF000000u) {
                int32_t part[4];
                int32_t place[4];
                place[0] = Add(shift, from);
                part[0] = from;
                part[2] = to;
                place[1] = y;
                part[1] = row;
                place[2] = Add(shift, to);
                place[3] = Add(y, 1);
                part[3] = Add(row, 1);
                Virtual<void>(target, image::kCopy, source, &part[0], &place[0], uint32_t{0});
                continue;
            }

            int32_t x = Add(shift, from);
            for (int32_t column = from; column < to; ++column, ++x) {
                const uint32_t pixel = pixels[column];
                const uint32_t alpha = pixel >> 24;
                if (alpha == 0)
                    continue;
                uint8_t underRed;
                uint8_t underGreen;
                uint8_t underBlue;
                const uint32_t under = Virtual<uint32_t>(target, image::kGetPixel, x, y);
                Virtual<void>(target, image::kToRgb, under, &underRed, &underGreen, &underBlue);
                const uint32_t rest = 0xFF - alpha;
                uint32_t red = ((uint32_t{underRed} * rest + 0x80) >> 8) + ((pixel >> 16) & 0xFF);
                uint32_t green = ((uint32_t{underGreen} * rest + 0x80) >> 8) + ((pixel >> 8) & 0xFF);
                uint32_t blue = ((uint32_t{underBlue} * rest + 0x80) >> 8) + (pixel & 0xFF);
                if (red > 0xFF)
                    red = 0xFF;
                if (green > 0xFF)
                    green = 0xFF;
                if (blue > 0xFF)
                    blue = 0xFF;
                const uint32_t colour = Virtual<uint32_t>(target, image::kColour, red, green, blue);
                Virtual<void>(target, image::kSetPixel, x, y, colour);
            }
        }
        Virtual<void>(source, image::kUnlock, uint32_t{0x40});
    }
    Virtual<void>(target, image::kUnlock, uint32_t{0x8040});
}

__declspec(naked) void BlendRunsThunk()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push edi
    __asm push eax
    __asm push ecx
    __asm call BlendRuns
    __asm add esp, 20
    __asm ret
}

// The game's 0x007B2DD0 (in EAX a rectangle on the target, in ESI the target;
// on the stack a vector of runs with values and a colour, cleaned by the
// caller). Tints the target: each run (y * 0x10000 + first x, the x after the
// last, then one value per pixel) is placed by the rectangle's corner, cut to
// the rectangle and the target, and every pixel with a value moves towards
// the colour by value / 256 of the difference, channel by channel.
void __cdecl TintRuns(const int32_t* place, void* target, DwordVector* runs, uint32_t colour)
{
    if (runs->begin == runs->end)
        return;
    const int32_t* const area = Virtual<const int32_t*>(target, image::kArea);
    const int32_t areaLeft = area[0];
    const int32_t areaTop = area[1];
    const int32_t areaRight = area[2];
    const int32_t areaBottom = area[3];
    const int32_t left = place[0];
    const int32_t top = place[1];
    int32_t clipLeft = left;
    int32_t clipTop = top;
    int32_t clipRight = place[2];
    int32_t clipBottom = place[3];

    int32_t cut = Sub(areaLeft, left);
    if (cut > 0)
        clipLeft = Add(clipLeft, cut);
    cut = Sub(areaTop, top);
    if (cut > 0)
        clipTop = Add(clipTop, cut);
    cut = Sub(areaRight, clipRight);
    if (cut < 0)
        clipRight = Add(clipRight, cut);
    cut = Sub(areaBottom, clipBottom);
    if (cut < 0)
        clipBottom = Add(clipBottom, cut);
    if (Sub(clipRight, clipLeft) <= 0)
        return;
    if (Sub(clipBottom, clipTop) <= 0)
        return;

    if (!Virtual<uint8_t>(target, image::kLock, uint32_t{0x8080}))
        return;
    const uint32_t tintRed = (colour >> 16) & 0xFF;
    const uint32_t tintGreen = (colour >> 8) & 0xFF;
    const uint32_t tintBlue = colour & 0xFF;

    const uint32_t* run = runs->begin;
    const uint32_t* const last = runs->end - 1;
    if (!(run > last)) {
        do {
            const uint32_t first = run[0];
            const uint32_t second = run[1];
            const int32_t start = Add(static_cast<int32_t>(first & 0xFFFF), left);
            const int32_t end = Add(static_cast<int32_t>(second & 0xFFFF), left);
            const int32_t y = Add(static_cast<int32_t>(first >> 16), top);
            const uint32_t* const values = run + 2;
            run = values + Sub(end, start);

            if (y < clipTop || y >= clipBottom)
                continue;
            int32_t x = start;
            int32_t stop = end;
            if (start < clipLeft)
                x = clipLeft;
            if (end > clipRight)
                stop = clipRight;
            const uint32_t* value = values + Sub(x, start);
            if (!(x < stop))
                continue;
            do {
                const uint32_t cover = *value++;
                if (cover != 0) {
                    // Dwords in the original, of which the picture sets the low bytes.
                    uint32_t red = 0;
                    uint32_t green = 0;
                    uint32_t blue = 0;
                    const uint32_t under = Virtual<uint32_t>(target, image::kGetPixel, x, y);
                    Virtual<void>(target, image::kToRgb, under, reinterpret_cast<uint8_t*>(&red),
                                  reinterpret_cast<uint8_t*>(&green), reinterpret_cast<uint8_t*>(&blue));
                    const uint32_t oldRed = red & 0xFF;
                    red = (red & 0xFFFFFF00u) | ((oldRed + (((tintRed - oldRed) * cover + 0x80) >> 8)) & 0xFF);
                    const uint32_t oldGreen = green & 0xFF;
                    green = (green & 0xFFFFFF00u) |
                            ((oldGreen + (((tintGreen - oldGreen) * cover + 0x80) >> 8)) & 0xFF);
                    const uint32_t oldBlue = blue & 0xFF;
                    blue = (blue & 0xFFFFFF00u) | ((oldBlue + (((tintBlue - oldBlue) * cover + 0x80) >> 8)) & 0xFF);
                    const uint32_t mixed = Virtual<uint32_t>(target, image::kColour, red, green, blue);
                    Virtual<void>(target, image::kSetPixel, x, y, mixed);
                }
                ++x;
            } while (x < stop);
        } while (run <= last);
    }
    Virtual<void>(target, image::kUnlock, uint32_t{0x8040});
}

__declspec(naked) void TintRunsThunk()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push esi
    __asm push eax
    __asm call TintRuns
    __asm add esp, 16
    __asm ret
}

// The game's 0x007B3030 (tile record, int32_t* x, int32_t* y): where the
// tile's picture goes in the window: the floor of its map place less the
// view's place, and for y less the picture's height too (the map place is the
// picture's bottom left corner). The y difference passes through a float.
void __fastcall TilePlace(char* self, void*, char* record, int32_t* outX, int32_t* outY)
{
    const double mapX = Cdecl<double>(addr::CRT_floor, static_cast<double>(At<float>(record, tile::kMapX)));
    *outX = Truncate(mapX - static_cast<double>(At<int32_t>(self, view::kViewX)));
    const float recordY = At<float>(record, tile::kMapY);
    void* const picture = At<void*>(record, tile::kPicture);
    const double mapY = Cdecl<double>(addr::CRT_floor, static_cast<double>(recordY));
    const float fromView = static_cast<float>(mapY - static_cast<double>(At<int32_t>(self, view::kViewY)));
    const uint32_t height = Virtual<uint32_t>(picture, image::kHeight);
    *outY = Truncate(static_cast<double>(fromView) - static_cast<double>(height));
}

// The game's 0x007B30B0: two flag bytes, and everything is composed again.
uint32_t __fastcall SetFlags114(char* self, void*, uint8_t first, uint8_t second)
{
    At<uint8_t>(self, view::kFlag114) = first;
    At<uint8_t>(self, view::kFlag115) = second;
    return Invalidate(self, nullptr);
}

// The game's 0x007B30D0: show the city names or not.
uint32_t __fastcall SetNames(char* self, void*, uint8_t on)
{
    At<uint8_t>(self, view::kNames) = on;
    return Invalidate(self, nullptr);
}

// The game's 0x007B30F0: the map mode.
uint32_t __fastcall SetMode(char* self, void*, uint32_t mode)
{
    At<uint32_t>(self, view::kMode) = mode;
    return Invalidate(self, nullptr);
}

// The game's 0x007B3110 (tile record, int32_t rect[4]): the rectangle of the
// tile's picture in map pixels.
void __fastcall TileArea(char* self, void*, char* record, int32_t* rect)
{
    TilePlace(self, nullptr, record, &rect[0], &rect[1]);
    rect[0] = Add(rect[0], At<int32_t>(self, view::kViewX));
    rect[1] = Add(rect[1], At<int32_t>(self, view::kViewY));
    const int32_t* area = Virtual<const int32_t*>(At<void*>(record, tile::kPicture), image::kArea);
    rect[2] = Add(Sub(area[2], area[0]), rect[0]);
    area = Virtual<const int32_t*>(At<void*>(record, tile::kPicture), image::kArea);
    rect[3] = Add(Sub(area[3], area[1]), rect[1]);
}

// The game's 0x007B3170 (regional city, string; `this` is not used): empties
// the string when the city is a tutorial whose record in the tutorial
// registry has its byte at +0x10 set.
void __fastcall BlankFlaggedTutorialText(char*, void*, void* city, void* text)
{
    void* const registry = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xC4); // GetTutorialRegistry
    if (!Virtual<uint8_t>(city, 0x10C)) // IsTutorial
        return;
    const uint32_t tutorial = Virtual<uint32_t>(city, 0x104); // GetTutorialGUID
    char* const found = This<char*>(addr::cSC4TutorialRegistry_Find, registry, tutorial);
    if (!found || !At<uint8_t>(found, 0x10))
        return;
    Virtual<void>(text, 0x3C, uint32_t{0}); // Resize
}

// The game's 0x007B31D0 (first, last, value): every cell in the range becomes
// a copy of `value`.
void __cdecl FillCells(Cell* first, Cell* last, const Cell* value)
{
    if (first == last)
        return;
    do {
        if (value != first)
            Assign(first->picture, value->picture);
        first->stale = value->stale;
        ++first;
    } while (first != last);
}

// The game's 0x007B3220 (first, last, out): copies the cells forwards and
// returns the end of the copy.
Cell* __cdecl CopyCells(Cell* first, Cell* last, Cell* out)
{
    int32_t count = Sub(static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(last)),
                        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(first))) >> 3;
    if (count <= 0)
        return out;
    do {
        if (first != out)
            Assign(out->picture, first->picture);
        out->stale = first->stale;
        ++first;
        ++out;
    } while (--count != 0);
    return out;
}

// The game's 0x007B3290 (first, last, out end): copies the cells backwards
// and returns the start of the copy.
Cell* __cdecl CopyCellsBackward(Cell* first, Cell* last, Cell* outEnd)
{
    int32_t count = Sub(static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(last)),
                        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(first))) >> 3;
    if (count <= 0)
        return outEnd;
    do {
        --last;
        --outEnd;
        if (last != outEnd)
            Assign(outEnd->picture, last->picture);
        outEnd->stale = last->stale;
    } while (--count != 0);
    return outEnd;
}

bool InstallRegionView1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionView_PointIsOwn, reinterpret_cast<void*>(&PointIsOwn)},
        {addr::SC4RegionViewLabelProc_DoWinProc, reinterpret_cast<void*>(&FlashProc)},
        {addr::SC4RegionViewGrid_MarkStale, reinterpret_cast<void*>(&GridMarkStale)},
        {addr::SC4RegionViewGrid_Paint, reinterpret_cast<void*>(&GridPaint)},
        {addr::cSC4WinRegionView_Invalidate, reinterpret_cast<void*>(&Invalidate)},
        {addr::cSC4WinRegionView_BlendRuns, reinterpret_cast<void*>(&BlendRunsThunk)},
        {addr::cSC4WinRegionView_TintRuns, reinterpret_cast<void*>(&TintRunsThunk)},
        {addr::cSC4WinRegionView_TilePlace, reinterpret_cast<void*>(&TilePlace)},
        {addr::cSC4WinRegionView_SetFlags114, reinterpret_cast<void*>(&SetFlags114)},
        {addr::cSC4WinRegionView_SetNames, reinterpret_cast<void*>(&SetNames)},
        {addr::cSC4WinRegionView_SetMode, reinterpret_cast<void*>(&SetMode)},
        {addr::cSC4WinRegionView_TileArea, reinterpret_cast<void*>(&TileArea)},
        {addr::cSC4WinRegionView_BlankFlaggedTutorialText, reinterpret_cast<void*>(&BlankFlaggedTutorialText)},
        {addr::SC4RegionViewGrid_FillCellRange, reinterpret_cast<void*>(&FillCells)},
        {addr::SC4RegionViewGrid_CopyCellRange, reinterpret_cast<void*>(&CopyCells)},
        {addr::SC4RegionViewGrid_CopyCellRangeBackward, reinterpret_cast<void*>(&CopyCellsBackward)},
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

const bool g_regionView1Registered = registry::Register({
    "region.view.part1",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionView_Invalidate,
    "Our region map window, part 1: the picture grid (stale cells, painting, the draw callback), the two pixel "
    "loops of the tile composer, a tile's place and rectangle, the names, flags and mode setters, the bobbing "
    "window procedure",
    false,
    InstallRegionView1,
});

} // namespace
} // namespace opensc4::region
