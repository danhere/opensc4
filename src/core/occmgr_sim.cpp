// Our cSC4OccupantManager (build 641; the class map is in occmgr_sim.h):
// the conversions between positions, world cells, occupant manager cells
// and standard city cells, Init and Shutdown, the save record and our own
// object (constructor, destructor, identity). The edits are in
// occmgr_edit.cpp, the queries in occmgr_queries.cpp.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the manager's own slots (0x00A8F7F0), the occupants (57 vtables;
// slots 1 to 4, 6, 25 and 26 here), the lock 0x00AD1EF4, the type filter
// 0x00A8F79C, the stream parts 0x00AA2310 and 0x00AA2280 and the allocator
// service (slots 3 and 4, as the game's list helpers call it).
//
// Floating point: the game computes on the x87 stack at 53 bits and stores
// floats; doubles for the intermediates here, floats where it stores.
#include "core/occmgr_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/settings.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::occmgr_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("occmgr: first %s", name);
}

bool SwapSlots(const Slot* slots, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("occmgr %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("occmgr %s: %u slots are ours", what, static_cast<unsigned>(count));
    return true;
}

namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using SelfFn = void(__thiscall*)(void* self);
using ConstructPartFn = void*(__thiscall*)(void* self);
using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const void* value);
using FillInsertFn = void(__thiscall*)(void* vector, void* where, uint32_t count, const void* value);
using SetInsertFn = uint8_t(__thiscall*)(void* set, void* key);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);
const auto ResizeColumns = reinterpret_cast<ResizeFn>(addr::SC4LotColumns_Resize);
const auto FillInsert = reinterpret_cast<FillInsertFn>(addr::SC4PtrVector_FillInsert);
const auto SetInsert = reinterpret_cast<SetInsertFn>(addr::SC4VisitedSet_Insert);

} // namespace

Cell* NewCell()
{
    Cell* const cell = static_cast<Cell*>(OperatorNew(4));
    if (!cell)
        return nullptr;
    cell->head = nullptr;
    ListNode* const head = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
    head->next = head;
    head->previous = head;
    cell->head = head;
    return cell;
}

ListNode* NewNodeList()
{
    ListNode* const head = Virtual<ListNode*>(ListNodeAllocator(), 0xC, 0xCu);
    head->next = head;
    head->previous = head;
    return head;
}

void AppendNode(ListNode* head, void* occupant)
{
    ListNode* const node = Virtual<ListNode*>(ListNodeAllocator(), 0xC, 0xCu);
    node->value = reinterpret_cast<std::uintptr_t>(occupant);
    ListNode* const last = head->previous;
    node->previous = last;
    node->next = head;
    last->next = node;
    head->previous = node;
}

bool BigCities()
{
    static const bool on = [] {
        if (!settings::GetBool("improvements", "big_cities", false))
            return false;
        for (const char* id : {"simulators.occmgr.cells", "simulators.occmgr.edit", "simulators.occmgr.queries",
                               "simulators.occmgr.init", "simulators.occmgr.save", "simulators.occmgr.object",
                               "simulators.weather.bodies", "simulators.traffic.object",
                               "simulators.traffic.pathfinder", "simulators.traffic.interface",
                               "simulators.traffic.step", "simulators.traffic.substep", "simulators.traffic.month",
                               "simulators.traffic.messages", "simulators.traffic.phases", "simulators.traffic.save",
                               "simulators.traffic.load", "simulators.traffic.shutdown", "simulators.traffic.init",
                               "simulators.traffic.cityinited", "simulators.traffic.routing",
                               "simulators.traffic.helpers", "simulators.traffic.tuning", "simulators.traffic.walks",
                               "simulators.traffic.waterroutes"}) {
            if (registry::ModeOf(id) != registry::Mode::Ours) {
                log::Write("big cities: off, %s is not ours (the game's own code knows only 256 cells)", id);
                return false;
            }
        }
        log::Write("big cities: on");
        return true;
    }();
    return on;
}

int32_t NewGridCells(char* self)
{
    if (!BigCities())
        return kWorldCells;
    int32_t city = At<int32_t>(self, field::kCellsX);
    if (At<int32_t>(self, field::kCellsZ) > city)
        city = At<int32_t>(self, field::kCellsZ);
    const int32_t cells = 2 * kCityOrigin + city;
    return cells < kWorldCells ? kWorldCells : (cells > 0xFF ? 0xFF : cells);
}

// The set's count and hash count zeroed, its key vector emptied and its
// spare-entry cursor (+0x48 of the set) put back at the deque's end (+0x30).
void ResetVisited(char* self)
{
    char* const set = self + field::kVisited;
    At<uint32_t>(set, 0x0) = 0;
    At<uint32_t>(set, 0x4) = 0;
    At<char*>(set, 0xC) = At<char*>(set, 0x8);
    std::memcpy(set + 0x48, set + 0x30, 16);
}

namespace {

// --- The conversions ----------------------------------------------------------------
// The slots the header calls bool return whatever EAX held: the last
// pointer or value written. Kept, so a caller that tests AL sees the same.

// The game's round to nearest: 12582912.0 (1.5 * 2^23) added, stored as a
// float, and 0x4B400000 taken from its bits.
int32_t RoundTrick(double value)
{
    const float stored = static_cast<float>(value + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &stored, sizeof bits);
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

constexpr double kPerCell = 0.015625;                      // 0x00A8F7B8: 1 / 64
constexpr double kPerCityCell = 0.0625;                    // 0x00A8E174: 1 / 16
constexpr double kBias = static_cast<double>(0.499f);      // 0x00A8F7BC
constexpr double kWorldOffset = 4096.0;                    // 0x00A8F7C0

float FloatOf(uint32_t shifted) { return static_cast<float>(static_cast<int32_t>(shifted)); }

// 6 (0x0049D990).
float* __fastcall GetOccupantManagerCellSizes(char*, void*, float* x, float* z)
{
    static bool logged = false;
    FirstCall("GetOccupantManagerCellSizes", logged);
    *x = 64.0f;
    *z = 64.0f;
    return x;
}

// 7 (0x0049D9B0).
int32_t* __fastcall GetWorldCellCount(char* self, void*, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("GetWorldCellCount", logged);
    *x = WorldCells(self);
    *z = WorldCells(self);
    return x;
}

// 8 (0x0049D9D0).
int32_t __fastcall GetOccupantManagerCellCount(char* self, void*, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("GetOccupantManagerCellCount", logged);
    *x = At<int32_t>(self, field::kCellsX);
    const int32_t cellsZ = At<int32_t>(self, field::kCellsZ);
    *z = cellsZ;
    return cellsZ;
}

// 9 (0x0049DB50): the city's world cells.
int32_t __fastcall GetOccupantManagerCellBounds(char* self, void*, int32_t* minX, int32_t* minZ, int32_t* maxX,
                                                int32_t* maxZ)
{
    static bool logged = false;
    FirstCall("GetOccupantManagerCellBounds", logged);
    *minX = kCityOrigin;
    *minZ = kCityOrigin;
    *maxX = At<int32_t>(self, field::kCellsX) + 0x3F;
    const int32_t last = At<int32_t>(self, field::kCellsZ) + 0x3F;
    *maxZ = last;
    return last;
}

// 10 (0x0049D9F0).
int32_t* __fastcall WorldCellToOccupantManagerCell(char*, void*, int32_t x, int32_t z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("WorldCellToOccupantManagerCell", logged);
    *outX = static_cast<int32_t>(static_cast<uint32_t>(x) - 0x40u);
    *outZ = static_cast<int32_t>(static_cast<uint32_t>(z) - 0x40u);
    return outZ;
}

// 11 (0x0049DA10).
int32_t* __fastcall OccupantManagerCellToWorldCell(char*, void*, int32_t x, int32_t z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("OccupantManagerCellToWorldCell", logged);
    *outX = static_cast<int32_t>(static_cast<uint32_t>(x) + 0x40u);
    *outZ = static_cast<int32_t>(static_cast<uint32_t>(z) + 0x40u);
    return outZ;
}

// 12 (0x0049DA30). The game's bug, copied: x is never read, *outX gets
// z * 4 - 256 and *outZ is multiplied by four in place.
int32_t* __fastcall WorldCellToStandardCityCell(char*, void*, int32_t, int32_t z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("WorldCellToStandardCityCell", logged);
    *outX = static_cast<int32_t>(static_cast<uint32_t>(z) * 4u - 0x100u);
    *outZ = static_cast<int32_t>(static_cast<uint32_t>(*outZ) << 2);
    return outZ;
}

// 13 (0x0049DA50): the divisions round toward zero.
int32_t __fastcall StandardCityCellToWorldCell(char*, void*, int32_t x, int32_t z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("StandardCityCellToWorldCell", logged);
    *outX = x / 4 + 0x40;
    const int32_t cellZ = z / 4 + 0x40;
    *outZ = cellZ;
    return cellZ;
}

// 14 (0x0049DA80).
float* __fastcall WorldCellToPosition(char*, void*, int32_t x, int32_t z, float* outX, float* outZ)
{
    static bool logged = false;
    FirstCall("WorldCellToPosition", logged);
    *outX = FloatOf((static_cast<uint32_t>(x) - 0x40u) << 6);
    *outZ = FloatOf((static_cast<uint32_t>(z) - 0x40u) << 6);
    return outZ;
}

// 15 (0x0049E0E0): no range test; a position far outside gives a cell far
// outside.
int32_t* __fastcall PositionToWorldCell(char*, void*, float x, float z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("PositionToWorldCell", logged);
    *outX = RoundTrick((static_cast<double>(x) + kWorldOffset) * kPerCell - kBias);
    *outZ = RoundTrick((static_cast<double>(z) + kWorldOffset) * kPerCell - kBias);
    return outZ;
}

// 16 (0x0049DAF0).
float* __fastcall OccupantManagerCellToPosition(char*, void*, int32_t x, int32_t z, float* outX, float* outZ)
{
    static bool logged = false;
    FirstCall("OccupantManagerCellToPosition", logged);
    const float positionX = FloatOf(static_cast<uint32_t>(x) << 6);
    const float positionZ = FloatOf(static_cast<uint32_t>(z) << 6);
    *outX = positionX;
    *outZ = positionZ;
    return outZ;
}

// 17 (0x0049E150).
int32_t* __fastcall PositionToOccupantManagerCell(char*, void*, float x, float z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("PositionToOccupantManagerCell", logged);
    *outX = RoundTrick(static_cast<double>(x) * kPerCell - kBias);
    *outZ = RoundTrick(static_cast<double>(z) * kPerCell - kBias);
    return outZ;
}

// 18 (0x0049DB20).
float* __fastcall StandardCityCellToPosition(char*, void*, int32_t x, int32_t z, float* outX, float* outZ)
{
    static bool logged = false;
    FirstCall("StandardCityCellToPosition", logged);
    const float positionX = FloatOf(static_cast<uint32_t>(x) << 4);
    const float positionZ = FloatOf(static_cast<uint32_t>(z) << 4);
    *outX = positionX;
    *outZ = positionZ;
    return outZ;
}

// position / 16 - 0.5 rounded to nearest, and one more when position / 16
// is exactly that next whole number (so whole cells round down, not to
// even).
int32_t CityCellOf(float position)
{
    const double scaled = static_cast<double>(position) * kPerCityCell;
    const int32_t cell = RoundTrick(scaled - 0.5);
    const int32_t above = static_cast<int32_t>(static_cast<uint32_t>(cell) + 1u);
    return scaled == static_cast<double>(above) ? above : cell;
}

// 19 (0x0049E1B0). The game returns with the FPU status word in AX (its
// low byte the sticky exception flags), which cannot be had here: 1, as
// the inexact flag is as good as always set.
uint32_t __fastcall PositionToStandardCityCell(char*, void*, float x, float z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("PositionToStandardCityCell", logged);
    *outX = CityCellOf(x);
    *outZ = CityCellOf(z);
    return 1;
}

// 26 (0x0049DBA0): the neighbours of a world cell as (x, z) pairs, x then
// z, the cell itself and those outside the grid left out; the count. Away
// from the edges the game writes the eight without the tests.
int32_t __fastcall GetBoundingCells(char* self, void*, int32_t x, int32_t z, int32_t* cells, int32_t* count)
{
    static bool logged = false;
    FirstCall("GetBoundingCells", logged);
    if (x >= 1 && x < WorldCells(self) - 1 && z >= 1 && z < WorldCells(self) - 1) {
        const int32_t pairs[16] = {x - 1, z - 1, x - 1, z, x - 1, z + 1, x,     z - 1,
                                   x,     z + 1, x + 1, z - 1, x + 1, z, x + 1, z + 1};
        for (int i = 0; i < 16; ++i)
            cells[i] = pairs[i];
        *count = 8;
        return 8;
    }
    *count = 0;
    const int32_t lastX = static_cast<int32_t>(static_cast<uint32_t>(x) + 1u);
    const int32_t lastZ = static_cast<int32_t>(static_cast<uint32_t>(z) + 1u);
    int32_t cellX = static_cast<int32_t>(static_cast<uint32_t>(x) - 1u);
    if (cellX > lastX)
        return *count;
    do {
        int32_t cellZ = static_cast<int32_t>(static_cast<uint32_t>(z) - 1u);
        if (cellZ <= lastZ) {
            do {
                if (cellX >= 0 && cellX < WorldCells(self) && cellZ >= 0 && cellZ < WorldCells(self) &&
                    !(cellX == x && cellZ == z)) {
                    cells[*count * 2] = cellX;
                    cells[*count * 2 + 1] = cellZ;
                    ++*count;
                }
                cellZ = static_cast<int32_t>(static_cast<uint32_t>(cellZ) + 1u);
            } while (cellZ <= lastZ);
        }
        cellX = static_cast<int32_t>(static_cast<uint32_t>(cellX) + 1u);
    } while (cellX <= lastX);
    return *count;
}

#define OCCMGR_SLOT(index, name)                                                                                     \
    {                                                                                                                \
        addr::cSC4OccupantManager_vftable, index, addr::cSC4OccupantManager_##name, reinterpret_cast<void*>(&name)  \
    }

const Slot kCellSlots[] = {
    OCCMGR_SLOT(6, GetOccupantManagerCellSizes),
    OCCMGR_SLOT(7, GetWorldCellCount),
    OCCMGR_SLOT(8, GetOccupantManagerCellCount),
    OCCMGR_SLOT(9, GetOccupantManagerCellBounds),
    OCCMGR_SLOT(10, WorldCellToOccupantManagerCell),
    OCCMGR_SLOT(11, OccupantManagerCellToWorldCell),
    OCCMGR_SLOT(12, WorldCellToStandardCityCell),
    OCCMGR_SLOT(13, StandardCityCellToWorldCell),
    OCCMGR_SLOT(14, WorldCellToPosition),
    OCCMGR_SLOT(15, PositionToWorldCell),
    OCCMGR_SLOT(16, OccupantManagerCellToPosition),
    OCCMGR_SLOT(17, PositionToOccupantManagerCell),
    OCCMGR_SLOT(18, StandardCityCellToPosition),
    OCCMGR_SLOT(19, PositionToStandardCityCell),
    OCCMGR_SLOT(26, GetBoundingCells),
};

bool InstallCells(registry::Mode) { return SwapSlots(kCellSlots, sizeof kCellSlots / sizeof kCellSlots[0], "cells"); }

// --- Init and Shutdown ---------------------------------------------------------------

// The callback Init gives slot 44 after a load (0x0049D900): the
// occupant's own Init; go on.
uint8_t __cdecl InitOccupant(void* occupant, void*)
{
    Virtual<uint8_t>(occupant, occupant::kInit);
    return 1;
}

// 3 (0x004A18E0): once. The flag first; the filter by type when there is
// none (0x10 bytes: the shared base, the two vtables, type 0), referenced;
// then the grid. An empty grid (a new city) becomes 192 columns of 192
// null cells. A grid read from a save keeps its cells and every occupant
// in it is initialized through slot 44 (the whole city, no filter). True.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    LockOf(self);
    if (!At<uint8_t>(self, field::kInitialized)) {
        At<uint8_t>(self, field::kInitialized) = 1;
        if (!At<void*>(self, field::kFilter)) {
            char* filter = static_cast<char*>(OperatorNew(0x10));
            if (filter) {
                reinterpret_cast<ConstructPartFn>(addr::SC4_OccupantFilterBase)(filter);
                At<std::uintptr_t>(filter, 0x0) = addr::cSC4OccupantManager_TypeFilter_vftable;
                At<std::uintptr_t>(filter, 0x4) = addr::cSC4OccupantManager_TypeFilter_vftable_4;
                At<uint32_t>(filter, 0xC) = 0;
            }
            At<void*>(self, field::kFilter) = filter;
            AddRefOf(filter);
        }
        Vector* const grid = Grid(self);
        if (grid->begin == grid->end) {
            const Vector empty{nullptr, nullptr, nullptr};
            const int32_t cells = NewGridCells(self);
            ResizeColumns(grid, static_cast<uint32_t>(cells), &empty);
            const void* const null = nullptr;
            const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
            for (int32_t i = 0; i < columns; ++i) {
                Vector* const column = reinterpret_cast<Vector*>(grid->begin + i * 12);
                const uint32_t size = static_cast<uint32_t>((column->end - column->begin) >> 2);
                if (size > static_cast<uint32_t>(cells))
                    column->end = column->begin + cells * 4;
                else
                    FillInsert(column, column->end, static_cast<uint32_t>(cells) - size, &null);
            }
        } else {
            using CallbackFn = uint8_t(__cdecl*)(void*, void*);
            Virtual<uint8_t>(self, slot::kIterate, static_cast<CallbackFn>(&InitOccupant), static_cast<void*>(self),
                             static_cast<const int32_t*>(nullptr), static_cast<const int32_t*>(nullptr),
                             static_cast<void*>(nullptr));
        }
    }
    UnlockOf(self);
    return 1;
}

// 4 (0x0049E000): when initialized, every occupant removed and shut down
// (slot 22: no filter, shut down, data 0), the filter released and the
// flag cleared. The grid's columns stay (the destructor's). True.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    LockOf(self);
    if (At<uint8_t>(self, field::kInitialized)) {
        Virtual<uint8_t>(self, slot::kRemoveOccupants, static_cast<void*>(nullptr), 1u, 0u);
        if (void* const filter = At<void*>(self, field::kFilter)) {
            ReleaseOf(filter);
            At<void*>(self, field::kFilter) = nullptr;
        }
        At<uint8_t>(self, field::kInitialized) = 0;
    }
    UnlockOf(self);
    return 1;
}

const Slot kInitSlots[] = {
    OCCMGR_SLOT(3, Init),
    OCCMGR_SLOT(4, Shutdown),
};

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, 2, "init"); }

// --- Save -------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x0049ECF0), on the part at +0x4: version 1, the
// size (two floats), the four cell counts, the number of columns, and for
// each column its number of cells and for each cell the number of entries
// and the occupants as references, front to back (0 for a cell without a
// list or with an empty one). False when the stream is in error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - kSerializablePart;
    Virtual<uint8_t>(stream, 0x1C, 1u);
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kSizeX));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kSizeZ));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellsX));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellsZ));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCityCellsX));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCityCellsZ));
    Vector* const grid = Grid(self);
    Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>((grid->end - grid->begin) / 12));
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    for (int32_t i = 0; i < columns; ++i) {
        const Vector* column = reinterpret_cast<const Vector*>(grid->begin + i * 12);
        Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>((column->end - column->begin) >> 2));
        column = reinterpret_cast<const Vector*>(grid->begin + i * 12);
        const int32_t rows = static_cast<int32_t>((column->end - column->begin) >> 2);
        for (int32_t row = 0; row < rows; ++row) {
            const Cell* const cell =
                reinterpret_cast<Cell**>(reinterpret_cast<const Vector*>(grid->begin + i * 12)->begin)[row];
            if (!cell || cell->head->next == cell->head) {
                Virtual<uint8_t>(stream, 0x24, 0u);
                continue;
            }
            uint32_t count = 0;
            for (const ListNode* node = cell->head->next; node != cell->head; node = node->next)
                ++count;
            Virtual<uint8_t>(stream, 0x24, count);
            for (const ListNode* node = cell->head->next; node != cell->head; node = node->next)
                WriteRef(stream, OccupantOf(node));
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0u : 1u;
}

// cIGZSerializable slot 4 (0x004A1A20): the loaded flag set first, then
// version 1 only (false otherwise). The grid is resized to the saved
// columns and cells; a cell with entries gets its list and the occupants
// appended in the saved order, each with its one-cell bit worked out again
// from its recorded cells. Every reference read holds a count: an occupant
// of several cells keeps the first only (the visited set tells), so the
// manager holds one reference per occupant as after InsertOccupant. The
// game's version, counts and occupant locals are unset before their reads:
// zero here.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;
    At<uint8_t>(self, field::kLoaded) = 1;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kSizeX));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kSizeZ));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kCellsX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kCellsZ));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kCityCellsX));
    Virtual<uint8_t>(stream, 0x20, &At<int32_t>(self, field::kCityCellsZ));
    uint32_t columns = 0;
    Virtual<uint8_t>(stream, 0x24, &columns);
    Vector* const grid = Grid(self);
    const Vector empty{nullptr, nullptr, nullptr};
    ResizeColumns(grid, columns, &empty);
    const void* const null = nullptr;
    // One local each in the game, never reset: after a failed read every
    // later column keeps the last rows and every later cell the last count.
    uint32_t rows = 0, count = 0;
    for (uint32_t i = 0; i < columns; ++i) {
        Virtual<uint8_t>(stream, 0x24, &rows);
        Vector* const column = reinterpret_cast<Vector*>(grid->begin + i * 12);
        const uint32_t size = static_cast<uint32_t>((column->end - column->begin) >> 2);
        if (rows < size)
            column->end = column->begin + rows * 4;
        else
            FillInsert(column, column->end, rows - size, &null);
        for (uint32_t row = 0; row < rows; ++row) {
            Virtual<uint8_t>(stream, 0x24, &count);
            if (!count)
                continue;
            Cell* const cell = NewCell();
            reinterpret_cast<Cell**>(reinterpret_cast<Vector*>(grid->begin + i * 12)->begin)[row] = cell;
            for (uint32_t k = 0; k < count; ++k) {
                void* occupant = nullptr;
                if (!ReadRef(stream, kIidOccupant, &occupant, 0u))
                    continue;
                uint8_t rect[4] = {};
                uint32_t single = 0;
                if (Virtual<uint8_t>(occupant, occupant::kGetCells, static_cast<uint8_t*>(rect)))
                    single = rect[2] <= rect[0] && rect[3] <= rect[1] ? 1u : 0u;
                ListNode* const head = cell->head;
                ListNode* const node = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
                node->value = reinterpret_cast<std::uintptr_t>(occupant) + single;
                ListNode* const last = head->previous;
                node->next = head;
                node->previous = last;
                last->next = node;
                head->previous = node;
                if (!single && !SetInsert(self + field::kVisited, occupant))
                    ReleaseOf(occupant);
            }
        }
    }
    ResetVisited(self);
    return Virtual<uint8_t>(stream, 0x48) ? 0u : 1u;
}

const Slot kSaveSlots[] = {
    {addr::cSC4OccupantManager_vftable_4, 3, addr::cSC4OccupantManager_WriteRecord, reinterpret_cast<void*>(&Write)},
    {addr::cSC4OccupantManager_vftable_4, 4, addr::cSC4OccupantManager_ReadRecord, reinterpret_cast<void*>(&Read)},
};

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, 2, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.occmgr.cells", "simulators/occmgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4OccupantManager_PositionToWorldCell,
                        "Our occupant manager conversions between positions, world cells, occupant manager cells "
                        "and standard city cells, and the small getters (src/core/occmgr_sim.cpp)",
                        false, InstallCells}),
    registry::Register({"simulators.occmgr.init", "simulators/occmgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4OccupantManager_Init,
                        "Our occupant manager Init (the type filter, the empty grid or the loaded occupants' Init) "
                        "and Shutdown (src/core/occmgr_sim.cpp)",
                        false, InstallInit}),
    registry::Register({"simulators.occmgr.save", "simulators/occmgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4OccupantManager_vftable_4,
                        "Our occupant manager Write and Read, version 1: the grid cell by cell "
                        "(src/core/occmgr_sim.cpp)",
                        false, InstallSave}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00A8F7F0 (50 slots, deleting destructor last),
// 0x00A8F7D4 (at +4) and 0x00A8F7C4 (at +8), taken at the first
// construction: a city is made long after PostAppInit, so the other
// groups' slot swaps are in them.
void* g_vtable[kSlots];
void* g_vtable4[kSerializableSlots];
void* g_vtable8[kMessageSlots];

// As the game's (0x0049DF80): under the lock.
uint32_t __fastcall AddRef(char* self, void*)
{
    LockOf(self);
    const uint32_t refs = ++At<uint32_t>(self, field::kRefCount);
    UnlockOf(self);
    return refs;
}

// As the game's (0x0049D910). The game's null tests of `this` for the
// parts cannot be met.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidManager: offset = 0; break;
    case kIidSerializable: offset = kSerializablePart; break;
    case kIidMessageTarget: offset = kMessagePart; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x0049DFB0): under the lock the count down while above 1;
// at the last reference (not counted down) Shutdown and the deleting
// destructor (slot 49) after the lock.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    LockOf(self);
    const uint32_t refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1) {
        At<uint32_t>(self, field::kRefCount) = refs - 1;
        UnlockOf(self);
        return refs - 1;
    }
    UnlockOf(self);
    Virtual<uint8_t>(self, slot::kShutdown);
    Virtual<void*>(self, slot::kDeletingDestructor, 1u);
    return 0;
}

// The destructor (0x004A0620): the visited set (its deque, then its two
// vectors' buffers), the grid's columns (the cells' lists are Shutdown's)
// and the lock.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kSerializablePart) = g_vtable4;
    At<void**>(self, kMessagePart) = g_vtable8;
    char* const set = self + field::kVisited;
    reinterpret_cast<SelfFn>(addr::SC4VisitedSet_DestroyDeque)(set + 0x20);
    if (void* const buffer = At<void*>(set, 0x14))
        Free(buffer);
    if (void* const buffer = At<void*>(set, 0x8))
        Free(buffer);
    reinterpret_cast<SelfFn>(addr::SC4ShortVectorVector_Destroy)(Grid(self));
    reinterpret_cast<SelfFn>(addr::RZCriticalSection_Destroy)(self + field::kLock);
}

// Slot 49, as the game's (0x004A18C0).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("occmgr object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x004A05B0).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4OccupantManager_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4OccupantManager_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4OccupantManager_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[kSlots - 1] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kSerializablePart>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<kSerializablePart>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<kSerializablePart>);
    g_vtable4[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kMessagePart>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<kMessagePart>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<kMessagePart>);
    log::Write("occmgr object: vtables copied (slots 3/15/20/36 %p %p %p %p, +4 slots 3/4 %p %p)", g_vtable[3],
               g_vtable[15], g_vtable[20], g_vtable[36], g_vtable4[3], g_vtable4[4]);
}

// The constructor (detour of 0x004A0540, reached only by the jump from the
// factory 0x004A18A0): the fields the game's sets, in its order: the flags
// and the count, the lock, the sizes, filter and grid zeroed, the visited
// set (the game's constructor: it allocates its deque).
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kSerializablePart) = g_vtable4;
    At<void**>(self, kMessagePart) = g_vtable8;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint8_t>(self, field::kLoaded) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    reinterpret_cast<ConstructPartFn>(addr::RZCriticalSection_Construct)(self + field::kLock);
    std::memset(self + field::kSizeX, 0, field::kVisited - field::kSizeX);
    reinterpret_cast<ConstructPartFn>(addr::SC4VisitedSet_Construct)(self + field::kVisited);
    static int created = 0;
    if (created++ < 3)
        log::Write("occmgr object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4OccupantManager_cSC4OccupantManager);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.occmgr.object",
    "simulators/occmgr",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4OccupantManager_cSC4OccupantManager,
    "Our cSC4OccupantManager object: constructor (detour), destructor, identity; methods from copies of the "
    "(patched) game vtables (src/core/occmgr_sim.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::occmgr_sim
