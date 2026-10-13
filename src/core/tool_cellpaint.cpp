// The Game of Life tool of the city view (0x004BBE50 to 0x004BC975).
// Replacement, off by default:
//
//   ui.tools.cellpaint: the view input control of 0x88 bytes (vtable
//     0x00A90458, message target 0x00A90448 at +0x28, ID 0x69701127) the view
//     makes for the cheat GOL (0x8973A69C), with the cell grid it holds.
//
// The tool runs Conway's Game of Life on a grid of one byte per city cell,
// wrapped at the edges, and shows the live cells through a map overlay (the
// helper the map window and the bulldoze tool use). Init fills the grid at
// random; the framework's frame service (0x86AD10EC, asked for under 0xFF)
// sends a message every frame and each one is a generation. Pressing the
// left button on a cell flips it and decides what a drag does: on a dead
// cell the drag paints live cells, on a live one it erases. R fills the grid
// at random again; C, Delete and Backspace empty it. Nothing of the city is
// read but the terrain's size and the picked point, and nothing is changed.
//
// The grid (0x24 bytes at +0x40): +0 a byte that stops the generations
// (never set), +4 what a drag does (0 paints, 1 erases), +8 and +0xC the
// width and height, +0x10 the cells (0 dead, else the age, at most 0xBF),
// +0x14 the cells of the generation before, +0x18 and +0x1C the functions
// called with (x, z, context) for a cell that lives and for one that does
// not, +0x20 their context.
//
// The rest of the tool: +0x2C the terrain (referenced), +0x30 the width of a
// cell and +0x34 one over it, +0x38 and +0x3C the cell the drag was last
// at, +0x64 the overlay (referenced), +0x68 the live cells as a vector of
// (x, z) pairs, +0x74 a string (a text loaded in Init and not used).
//
// Left the game's: 0x004BC3B0 and 0x004BC4A0, the overlay's own functions
// that take a vector of cells (the map window's drawing calls them);
// 0x004BC3B0 is called by address. The shared slots (QueryInterface
// 0x004B8480, the key-up slot 0x004BDBE0) are the base's (tool_base.cpp);
// the thunks of AddRef, Release and the target stay the game's.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace opensc4::tool_cellpaint {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;   // the message target part (vtable 0x00A90448)
constexpr std::size_t kTerrain = 0x2C;  // the terrain (referenced)
constexpr std::size_t kCell = 0x30;     // float: the width of a cell
constexpr std::size_t kPerCell = 0x34;  // float: one over it
constexpr std::size_t kLastX = 0x38;    // the cell the drag was last at, -1 for none
constexpr std::size_t kLastZ = 0x3C;
constexpr std::size_t kGrid = 0x40;     // the grid (0x24 bytes)
constexpr std::size_t kOverlay = 0x64;  // the map overlay (referenced)
constexpr std::size_t kLive = 0x68;     // vector of (x, z) pairs: begin, end, capacity
constexpr std::size_t kText = 0x74;     // a string (0x14 bytes)
} // namespace field

// The grid's fields.
namespace grid {
constexpr std::size_t kStopped = 0x00;  // byte: no generations
constexpr std::size_t kErase = 0x04;    // what a drag does: 0 paints, 1 erases
constexpr std::size_t kWidth = 0x08;
constexpr std::size_t kHeight = 0x0C;
constexpr std::size_t kCells = 0x10;    // this generation
constexpr std::size_t kBefore = 0x14;   // the one before
constexpr std::size_t kOnLive = 0x18;   // cdecl (x, z, context)
constexpr std::size_t kOnDead = 0x1C;
constexpr std::size_t kContext = 0x20;
} // namespace grid

constexpr uint32_t kToolId = 0x69701127;
constexpr uint32_t kCursorText = 0xA9C87D93;
constexpr uint32_t kFrameCode = 0xFF;
constexpr uint8_t kOldest = 0xBF;

using CellFn = void(__cdecl*)(uint32_t x, uint32_t z, void* context);

struct Pair {
    uint32_t x;
    uint32_t z;
};
static_assert(sizeof(Pair) == 8);

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

// x87 fistp as _ftol does: truncation, the "integer indefinite" for what does
// not fit.
int32_t Truncate(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0))
        return INT_MIN;
    return static_cast<int32_t>(value);
}

// The byte of a cell in one of the grid's two arrays, by the game's 32-bit
// arithmetic (no range check).
uint8_t* CellAt(char* cells, uint32_t width, uint32_t x, uint32_t z)
{
    return reinterpret_cast<uint8_t*>(cells) + static_cast<uint32_t>(width * z + x);
}

// Zeroes both arrays, the generation before first; an array that is not
// there is skipped.
void EmptyGrid(char* cellGrid)
{
    for (const std::size_t array : {grid::kBefore, grid::kCells}) {
        if (uint8_t* const cells = At<uint8_t*>(cellGrid, array)) {
            const uint32_t count = At<uint32_t>(cellGrid, grid::kHeight) * At<uint32_t>(cellGrid, grid::kWidth);
            for (uint32_t i = 0; i < count; ++i)
                cells[i] = 0;
        }
    }
}

// --- The grid --------------------------------------------------------------------------

// The game's 0x004BBE50 (cdecl): the grid's function for a live cell. The
// overlay of the tool in `context` gets the cell (0x007DCD30).
void __cdecl CellLives(uint32_t x, uint32_t z, char* context)
{
    This<void>(addr::SC4UI_MapOverlay_AddCell, At<void*>(context, field::kOverlay), x, z);
}

// The game's 0x004BBE90: fills the grid at random. A generator of its own,
// seeded from the clock (seed -1), and for every cell, row by row, one draw
// below 2 whose low byte goes into both arrays.
void __fastcall GridRandomize(char* cellGrid, void*)
{
    // 0x18 bytes: the frame of 0x004BBE90 keeps it in the 0x18 bytes of its
    // locals.
    alignas(4) char generator[0x18];
    {
        // Clock seeds count under the original's call site.
        const fixed_seed::SiteOverride site(0x004BBEA2);
        This<void*>(addr::SC4Random_Construct, &generator[0], uint32_t{0xFFFFFFFF});
    }
    for (uint32_t z = 0; z < At<uint32_t>(cellGrid, grid::kHeight); ++z) {
        for (uint32_t x = 0; x < At<uint32_t>(cellGrid, grid::kWidth); ++x) {
            const uint32_t draw = This<uint32_t>(addr::SC4Random_BelowLcg, &generator[0], uint32_t{2});
            const uint8_t drawn = static_cast<uint8_t>(draw);
            const uint32_t width = At<uint32_t>(cellGrid, grid::kWidth);
            *CellAt(At<char*>(cellGrid, grid::kBefore), width, x, z) = drawn;
            *CellAt(At<char*>(cellGrid, grid::kCells), width, x, z) = drawn;
        }
    }
}

// The game's 0x004BBF00 (ret 8): the drag reaches a cell. Painting (+4 is
// 0) makes the cell live (1) and, when it was dead in the generation
// before, 2 there; erasing (1) makes it dead in both. True when the
// generation before changed. No range check.
uint8_t __fastcall GridPaint(char* cellGrid, void*, uint32_t x, uint32_t z)
{
    const uint32_t width = At<uint32_t>(cellGrid, grid::kWidth);
    uint8_t* const before = CellAt(At<char*>(cellGrid, grid::kBefore), width, x, z);
    uint8_t* const cell = CellAt(At<char*>(cellGrid, grid::kCells), width, x, z);
    const uint32_t erase = At<uint32_t>(cellGrid, grid::kErase);
    if (erase == 0) {
        *cell = 1;
        if (*before == 0) {
            *before = 2;
            return 1;
        }
    } else if (erase == 1) {
        *cell = 0;
        if (*before != 0) {
            *before = 0;
            return 1;
        }
    }
    return 0;
}

// The game's 0x004BBF50: one generation. The two arrays change places; then
// for every cell, row by row, the live ones among its eight neighbours in
// the generation before are counted, the grid wrapped at its edges. Three
// neighbours make a dead cell live (1); two or three let a live one go on, a
// year older in both arrays until 0xBF; anything else is dead. The function
// at +0x18 is called for a cell that lives, the one at +0x1C for one that
// does not, when set.
void __fastcall GridStep(char* cellGrid, void*)
{
    if (At<uint8_t>(cellGrid, grid::kStopped))
        return;
    char* const swapped = At<char*>(cellGrid, grid::kCells);
    At<char*>(cellGrid, grid::kCells) = At<char*>(cellGrid, grid::kBefore);
    At<char*>(cellGrid, grid::kBefore) = swapped;
    for (uint32_t z = 0; z < At<uint32_t>(cellGrid, grid::kHeight); ++z) {
        for (uint32_t x = 0; x < At<uint32_t>(cellGrid, grid::kWidth); ++x) {
            const uint32_t width = At<uint32_t>(cellGrid, grid::kWidth);
            const uint32_t height = At<uint32_t>(cellGrid, grid::kHeight);
            char* const from = At<char*>(cellGrid, grid::kBefore);
            uint8_t* const before = CellAt(from, width, x, z);
            uint8_t* const cell = CellAt(At<char*>(cellGrid, grid::kCells), width, x, z);

            // The game reads the neighbours of an inner cell directly and
            // wraps those of an edge cell; the same eight bytes either way
            // (in a grid one cell wide or high a neighbour can be the cell
            // itself, and counts).
            const uint32_t up = (z + height - 1) % height;
            const uint32_t down = (height + z + 1) % height;
            const uint32_t left = (width + x - 1) % width;
            const uint32_t right = (width + x + 1) % width;
            const uint32_t around[8][2] = {{left, up},  {x, up},      {right, up}, {left, z},
                                           {right, z},  {left, down}, {x, down},   {right, down}};
            uint32_t neighbours = 0;
            for (const auto& place : around) {
                if (*CellAt(from, width, place[0], place[1]) != 0)
                    ++neighbours;
            }

            CellFn told;
            if (neighbours <= 1 || neighbours >= 4) {
                *cell = 0;
                told = At<CellFn>(cellGrid, grid::kOnDead);
            } else {
                uint8_t age = *before;
                if (neighbours == 3 && age == 0) {
                    *cell = 1;
                    told = At<CellFn>(cellGrid, grid::kOnLive);
                } else if (age == 0 || age == kOldest) {
                    *cell = age;
                    told = *before != 0 ? At<CellFn>(cellGrid, grid::kOnLive) : At<CellFn>(cellGrid, grid::kOnDead);
                } else {
                    ++age;
                    *before = age;
                    *cell = age;
                    told = At<CellFn>(cellGrid, grid::kOnLive);
                }
            }
            if (told)
                told(x, z, At<void*>(cellGrid, grid::kContext));
        }
    }
}

// The game's 0x004BC230 (ret 0xC): a grid of a new size. Both arrays are
// freed and made again (the generation before first), 16 standing in for a
// width or height of 0; then filled at random, or zeroed.
void __fastcall GridResize(char* cellGrid, void*, uint32_t width, uint32_t height, uint8_t random)
{
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(cellGrid, grid::kCells));
    At<void*>(cellGrid, grid::kCells) = nullptr;
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(cellGrid, grid::kBefore));
    At<void*>(cellGrid, grid::kBefore) = nullptr;
    At<uint32_t>(cellGrid, grid::kWidth) = width ? width : 0x10;
    At<uint32_t>(cellGrid, grid::kHeight) = height ? height : 0x10;
    const std::size_t before = At<uint32_t>(cellGrid, grid::kHeight) * At<uint32_t>(cellGrid, grid::kWidth);
    At<void*>(cellGrid, grid::kBefore) = Cdecl<void*>(addr::SC4_operator_new, before);
    const std::size_t cells = At<uint32_t>(cellGrid, grid::kHeight) * At<uint32_t>(cellGrid, grid::kWidth);
    At<void*>(cellGrid, grid::kCells) = Cdecl<void*>(addr::SC4_operator_new, cells);
    if (random)
        GridRandomize(cellGrid, nullptr);
    else
        EmptyGrid(cellGrid);
}

// --- The tool --------------------------------------------------------------------------

// The game's 0x004BBE70 (slot 18, OnMouseUpL, ret 0xC): the drag ends; the
// mouse is let go when the tool has it (slot 25). True.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (Virtual<uint8_t>(self, 0x64)) // AmCapturing
        This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    return 1;
}

// The game's 0x004BC4D0: the live cells of the grid, row by row, into the
// vector at +0x68 (emptied first; it grows by its size, at least one), and
// from there into the overlay in place of what it had.
void __fastcall Refresh(char* self, void*)
{
    const uint8_t* cell = At<uint8_t*>(self, field::kGrid + grid::kCells);
    Pair*& begin = At<Pair*>(self, field::kLive);
    Pair*& end = At<Pair*>(self, field::kLive + 4);
    Pair*& capacity = At<Pair*>(self, field::kLive + 8);
    end = begin;
    const uint32_t height = At<uint32_t>(self, field::kGrid + grid::kHeight);
    const uint32_t width = At<uint32_t>(self, field::kGrid + grid::kWidth);
    for (uint32_t z = 0; z < height; ++z) {
        for (uint32_t x = 0; x < width; ++x, ++cell) {
            if (*cell == 0)
                continue;
            if (end != capacity) {
                if (end) {
                    end->x = x;
                    end->z = z;
                }
                ++end;
                continue;
            }
            const uint32_t size = static_cast<uint32_t>(end - begin);
            const uint32_t room = size + (size < 1 ? 1 : size);
            Pair* const made =
                room ? Cdecl<Pair*>(addr::SC4_stl_allocate, std::size_t{room * sizeof(Pair)}) : nullptr;
            Pair* to = made;
            for (const Pair* from = begin; from != end; ++from, ++to) {
                if (to)
                    *to = *from;
            }
            if (to) {
                to->x = x;
                to->z = z;
            }
            if (begin)
                Cdecl<void>(addr::SC4_Free, begin);
            begin = made;
            capacity = made + room;
            end = to + 1;
        }
    }
    void* const overlay = At<void*>(self, field::kOverlay);
    At<void*>(overlay, 0x14) = At<void*>(overlay, 0x10); // its cells emptied
    This<void>(addr::SC4UI_MapOverlay_AppendCells, overlay, static_cast<void*>(&begin));
}

// The game's 0x004BC190 (slot 4): Shutdown. The cursor text 0xA9C87D93
// cleared; the overlay taken off the renderer (slot 0x84), stopped and let
// go; the grid's functions cleared and its arrays freed; the terrain let go;
// the frame service told to stop (slot 6, with 0xFF); then the base.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText); // ClearCursorText
    if (void* const overlay = At<void*>(self, field::kOverlay)) {
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x84, overlay);
        This<void>(addr::SC4UI_MapOverlay_Stop, At<void*>(self, field::kOverlay));
        Release(At<void*>(self, field::kOverlay));
        At<void*>(self, field::kOverlay) = nullptr;
    }
    char* const cellGrid = self + field::kGrid;
    At<CellFn>(cellGrid, grid::kOnLive) = nullptr;
    At<CellFn>(cellGrid, grid::kOnDead) = nullptr;
    At<void*>(cellGrid, grid::kContext) = nullptr;
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(cellGrid, grid::kCells));
    void* const before = At<void*>(cellGrid, grid::kBefore);
    At<void*>(cellGrid, grid::kCells) = nullptr;
    Cdecl<void>(addr::SC4_operator_delete, before);
    At<void*>(cellGrid, grid::kBefore) = nullptr;
    if (void* const terrain = At<void*>(self, field::kTerrain)) {
        Release(terrain);
        At<void*>(self, field::kTerrain) = nullptr;
    }
    void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<void>(frames, 0x18, static_cast<void*>(self + field::kTarget), kFrameCode);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004BC2F0 (slot 3 of the message target at +0x28, ret 4).
// `this` is the target part. Any message (the frame service's is the only
// one asked for): the overlay's cells are emptied and the grid goes one
// generation on, which fills them again. True.
uint8_t __fastcall DoMessage(char* target, void*, void*)
{
    char* const self = target - field::kTarget;
    This<void>(addr::SC4UI_OverlayClear, At<void*>(self, field::kOverlay));
    GridStep(self + field::kGrid, nullptr);
    return 1;
}

// The game's 0x004BC310: the constructor; the caller allocated 0x88 bytes.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4LifeTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4LifeTool_TargetVtable;
    At<void*>(self, field::kTerrain) = nullptr;
    At<float>(self, field::kCell) = 1.0f;
    At<float>(self, field::kPerCell) = 1.0f;
    At<int32_t>(self, field::kLastX) = -1;
    At<int32_t>(self, field::kLastZ) = -1;
    char* const cellGrid = self + field::kGrid;
    At<uint8_t>(cellGrid, grid::kStopped) = 0;
    At<uint32_t>(cellGrid, grid::kErase) = 0;
    At<uint32_t>(cellGrid, grid::kWidth) = 0;
    At<uint32_t>(cellGrid, grid::kHeight) = 0;
    At<void*>(cellGrid, grid::kCells) = nullptr;
    At<void*>(cellGrid, grid::kBefore) = nullptr;
    At<CellFn>(cellGrid, grid::kOnLive) = nullptr;
    At<CellFn>(cellGrid, grid::kOnDead) = nullptr;
    At<void*>(cellGrid, grid::kContext) = nullptr;
    At<void*>(self, field::kOverlay) = nullptr;
    At<void*>(self, field::kLive) = nullptr;
    At<void*>(self, field::kLive + 4) = nullptr;
    At<void*>(self, field::kLive + 8) = nullptr;
    At<char*>(self, field::kText + 0x4) = nullptr;
    At<char*>(self, field::kText + 0x8) = nullptr;
    At<char*>(self, field::kText + 0xC) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    At<char*>(self, field::kText + 0x4) = buffer;
    At<char*>(self, field::kText + 0x8) = buffer;
    At<char*>(self, field::kText + 0xC) = buffer + 8;
    *buffer = 0;
    At<uint32_t>(self, field::kText + 0x10) = 0;
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, base::kCursor) = 0;
    At<uint32_t>(self, base::kId) = kToolId;
    return self;
}

// The game's 0x004BC440: the destructor's body. The string's buffer and the
// vector's are freed, then the grid's two arrays, then the base destructor.
// The terrain and the overlay are Shutdown's to let go.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, field::kText + 0x4))
        Cdecl<void>(addr::SC4_Free, text);
    if (void* const live = At<void*>(self, field::kLive))
        Cdecl<void>(addr::SC4_Free, live);
    char* const cellGrid = self + field::kGrid;
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(cellGrid, grid::kCells));
    void* const before = At<void*>(cellGrid, grid::kBefore);
    At<void*>(cellGrid, grid::kCells) = nullptr;
    Cdecl<void>(addr::SC4_operator_delete, before);
    At<void*>(cellGrid, grid::kBefore) = nullptr;
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004BC420 (slot 26, ret 4): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004BC670 (slot 3): Init. The base Init; the frame service
// asked for its message (slot 5, with 0xFF); once the terrain with the
// width of a cell, and once the overlay: made, started, scaled to the cell,
// coloured from the four floats at 0x00A90438 (0, 0.8, 0.2, 0.7), handed to
// the renderer with 5 and 1000, and the terrain's cell overlay (slot 0x108)
// gets 0 in its slot 10. Then the grid at the terrain's size in cells,
// filled at random, with the function for a live cell and the tool as
// context; the overlay filled; and the text 0x4A56B5FB of group 0x6A231EAA
// loaded into the string at +0x74.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<void>(frames, 0x14, static_cast<void*>(self + field::kTarget), kFrameCode);
    if (!At<void*>(self, field::kTerrain)) {
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        At<void*>(self, field::kTerrain) = terrain;
        AddRef(terrain);
        const float cell = Virtual<float>(At<void*>(self, field::kTerrain), 0x18);
        At<float>(self, field::kCell) = cell;
        const double one = Global<float>(addr::SC4_FloatOneA81228);
        At<float>(self, field::kPerCell) = static_cast<float>(one / static_cast<double>(cell));
    }
    if (!At<void*>(self, field::kOverlay)) {
        void* const renderer = Global<void*>(addr::SC4_View43DD0);
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x50});
        void* const overlay = memory ? This<void*>(addr::SC4UI_MapOverlay_Construct, memory) : nullptr;
        At<void*>(self, field::kOverlay) = overlay;
        AddRef(overlay);
        This<void>(addr::SC4UI_MapOverlay_Start, At<void*>(self, field::kOverlay));
        This<void>(addr::SC4UI_MapOverlay_SetFlagD, At<void*>(self, field::kOverlay), uint32_t{1});
        This<void>(addr::SC4UI_MapOverlay_SetScale, At<void*>(self, field::kOverlay), At<float>(self, field::kCell),
                   At<float>(self, field::kCell));
        This<void>(addr::SC4UI_MapOverlay_SetFlagE, At<void*>(self, field::kOverlay), uint32_t{1});
        This<void>(addr::SC4UI_MapOverlay_SetFillColor, At<void*>(self, field::kOverlay),
                   reinterpret_cast<const float*>(addr::SC4LifeTool_Color));
        Virtual<void>(renderer, 0x80, At<void*>(self, field::kOverlay), uint32_t{5}, uint32_t{0x3E8});
        void* const cells = Virtual<void*>(At<void*>(self, field::kTerrain), 0x108);
        Virtual<void>(cells, 0x28, uint32_t{0});
    }
    void* const terrain = At<void*>(self, field::kTerrain);
    char* const cellGrid = self + field::kGrid;
    At<uint32_t>(cellGrid, grid::kErase) = 0;
    const uint32_t height = Virtual<uint32_t>(terrain, 0x20);
    const uint32_t width = Virtual<uint32_t>(terrain, 0x1C);
    GridResize(cellGrid, nullptr, width, height, 1);
    At<std::uintptr_t>(cellGrid, grid::kOnLive) = addr::SC4LifeTool_CellLives;
    At<CellFn>(cellGrid, grid::kOnDead) = nullptr;
    At<void*>(cellGrid, grid::kContext) = self;
    Refresh(self, nullptr);
    Cdecl<void>(addr::SC4_LoadLocalizedText, static_cast<void*>(self + field::kText), uint32_t{0x6A231EAA},
                uint32_t{0x4A56B5FB}, uint32_t{0});
    return 1;
}

// The game's 0x004BC7D0 (slot 14, OnKeyDown, ret 8). C, Delete and
// Backspace empty the grid and the overlay; R fills the grid at random and
// the overlay from it; true for those. Any other key: the opposite of the
// shared key stub's answer (0x00505BE0, true always), so false.
uint8_t __fastcall OnKeyDown(char* self, void*, int32_t key, uint32_t modifiers)
{
    if (key == 0x43 || key == 0x2E || key == 0x08) {
        EmptyGrid(self + field::kGrid);
        This<void>(addr::SC4UI_OverlayClear, At<void*>(self, field::kOverlay));
        return 1;
    }
    if (key == 0x52) {
        GridRandomize(self + field::kGrid, nullptr);
        Refresh(self, nullptr);
        return 1;
    }
    return This<uint8_t>(addr::SC4ViewInputControl_OnKeyDefault, self, key, modifiers) ? 0 : 1;
}

// The cell under a terrain point: each of x and z times one over the cell's
// width, truncated.
void CellOf(char* self, const float* position, int32_t* x, int32_t* z)
{
    const double perCell = static_cast<double>(At<float>(self, field::kPerCell));
    *x = Truncate(static_cast<double>(position[0]) * perCell);
    *z = Truncate(static_cast<double>(position[2]) * perCell);
}

// The game's 0x004BC870 (slot 16, OnMouseDownL, ret 0xC). With a terrain
// point under the pointer (the view's PickTerrain, slot 36, with 0): the
// mouse is captured, the cell kept at +0x38 and +0x3C, the drag erases when
// the cell is live and paints when it is dead, the cell is painted so and
// the overlay filled again. The cell is not checked against the grid. True
// always.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float position[3];
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, &position[0], uint32_t{0}))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
    At<int32_t>(self, field::kLastX) = -1;
    At<int32_t>(self, field::kLastZ) = -1;
    int32_t cellX;
    int32_t cellZ;
    CellOf(self, position, &cellX, &cellZ);
    At<int32_t>(self, field::kLastX) = cellX;
    At<int32_t>(self, field::kLastZ) = cellZ;
    char* const cellGrid = self + field::kGrid;
    const uint8_t* const cell = CellAt(At<char*>(cellGrid, grid::kCells), At<uint32_t>(cellGrid, grid::kWidth),
                                       static_cast<uint32_t>(cellX), static_cast<uint32_t>(cellZ));
    At<uint32_t>(cellGrid, grid::kErase) = *cell != 0 ? 1 : 0;
    GridPaint(cellGrid, nullptr, static_cast<uint32_t>(cellX), static_cast<uint32_t>(cellZ));
    Refresh(self, nullptr);
    return 1;
}

// The game's 0x004BC900 (slot 20, OnMouseMove, ret 0xC). While the tool has
// the mouse and there is a terrain point under the pointer (PickTerrain
// with 1): when the cell is not the one the drag was last at, it is kept,
// painted and the overlay filled again. True always.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (!Virtual<uint8_t>(self, 0x64)) // AmCapturing
        return 1;
    float position[3];
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, &position[0], uint32_t{1}))
        return 1;
    int32_t cellX;
    int32_t cellZ;
    CellOf(self, position, &cellX, &cellZ);
    if (cellX == At<int32_t>(self, field::kLastX) && cellZ == At<int32_t>(self, field::kLastZ))
        return 1;
    At<int32_t>(self, field::kLastX) = cellX;
    At<int32_t>(self, field::kLastZ) = cellZ;
    GridPaint(self + field::kGrid, nullptr, static_cast<uint32_t>(cellX), static_cast<uint32_t>(cellZ));
    Refresh(self, nullptr);
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallCellPaint(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4LifeTool_CellLives, reinterpret_cast<void*>(&CellLives)},
        {addr::SC4LifeTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4LifeGrid_Randomize, reinterpret_cast<void*>(&GridRandomize)},
        {addr::SC4LifeGrid_Paint, reinterpret_cast<void*>(&GridPaint)},
        {addr::SC4LifeGrid_Step, reinterpret_cast<void*>(&GridStep)},
        {addr::SC4LifeTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4LifeGrid_Resize, reinterpret_cast<void*>(&GridResize)},
        {addr::SC4LifeTool_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4LifeTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4LifeTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4LifeTool_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4LifeTool_Refresh, reinterpret_cast<void*>(&Refresh)},
        {addr::SC4LifeTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4LifeTool_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::SC4LifeTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4LifeTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.cellpaint",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4LifeGrid_Step,
    "Our Game of Life tool of the city view, the cheat GOL (docs/tools.md)",
    false,
    InstallCellPaint,
});

} // namespace
} // namespace opensc4::tool_cellpaint
