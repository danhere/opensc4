// Our cSC4OccupantManager, the edits (build 641; the class map is in
// occmgr_sim.h): SetCitySize, InsertOccupant, RemoveOccupant, the two
// RemoveOccupants, MoveOccupant and FindOccupant, with the helpers only
// they use.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the occupants (57 vtables; slots 1 to 4, 6, 10, 15, 25, 26, 29,
// 31 and 32: 0, 0, 0, 0, 0, 2, 2, 1, 1, 1, 1 and 1 arguments in every one),
// the filter's slot 3 (1 argument in the manager's own 0x00A8F79C; a
// caller's filter is the caller's class), the manager's slots 15 and 22,
// the lock and the allocator service.
//
// The order of a cell's list is game state: a new entry goes to the FRONT
// (the game's push is an insert before the first node), a removal unlinks
// in place, and a cell whose list empties is freed and its grid entry
// cleared.
#include "core/occmgr_sim.h"

#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::occmgr_sim {
namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void(__thiscall*)(void* self);

const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto DestroyList = reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy);
const auto ClearNodeList = reinterpret_cast<SelfFn>(addr::SC4List_Clear2);

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// An entry at the front of a cell's list (a 0xC-byte node from the STL
// allocator before the first node).
void InsertFront(Cell* cell, std::uintptr_t value)
{
    ListNode* const first = cell->head->next;
    ListNode* const node = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
    node->value = value;
    ListNode* const before = first->previous;
    node->next = first;
    node->previous = before;
    before->next = node;
    first->previous = node;
}

void EraseNode(ListNode* node)
{
    ListNode* const next = node->next;
    ListNode* const previous = node->previous;
    previous->next = next;
    next->previous = previous;
    Free(node);
}

// 0x0049DAC0: a position's world cell (through slot 15), each coordinate's
// low byte. No clamp: a cell outside 0..255 wraps.
void CellBytes(char* self, float x, float z, uint8_t* outX, uint8_t* outZ)
{
    int32_t cellX = 0, cellZ = 0;
    Virtual<uint32_t>(self, slot::kPositionToWorldCell, x, z, &cellX, &cellZ);
    *outX = static_cast<uint8_t>(cellX);
    *outZ = static_cast<uint8_t>(cellZ);
}

// 0x0049EE80: the list of a world cell (clamped to the grid), made when
// asked and missing.
Cell* GetCell(char* self, int32_t x, int32_t z, bool create)
{
    x = ClampCell(x);
    z = ClampCell(z);
    Cell* cell = CellAt(self, x, z);
    if (!cell && create) {
        cell = NewCell();
        CellAt(self, x, z) = cell;
    }
    return cell;
}

// 0x0049F7C0: the list of the occupant's first cell: its recorded cells'
// low corner, or, with none recorded, the world cell of its box's low
// corner (x pulled in by 0.1 when the box is wider than 1; z is not). An
// occupant without a property holder leaves the caller's x and z as they
// were (unset stack in the game's only caller: zero there). The cell is
// clamped for the lookup only.
Cell* FindCell(char* self, void* occupant, int32_t* x, int32_t* z)
{
    if (Virtual<void*>(occupant, occupant::kPropertyHolder)) {
        uint8_t rect[4] = {};
        if (Virtual<uint8_t>(occupant, occupant::kGetCells, static_cast<uint8_t*>(rect))) {
            *x = rect[0];
            *z = rect[1];
        } else {
            float box[6] = {};
            Virtual<void*>(occupant, occupant::kGetBoundingBox, &box[0], &box[3]);
            ShrinkAxis(box[0], box[3]);
            Virtual<uint32_t>(self, slot::kPositionToWorldCell, box[0], box[2], x, z);
        }
    }
    return CellAt(self, ClampCell(*x), ClampCell(*z));
}

// 5 (0x0049E050): false once initialized. The size as given, its 64ths
// (the city's world cells) and its 16ths (standard city cells), truncated.
uint8_t __fastcall SetCitySize(char* self, void*, float x, float z)
{
    static bool logged = false;
    FirstCall("SetCitySize", logged);
    LockOf(self);
    if (At<uint8_t>(self, field::kInitialized)) {
        UnlockOf(self);
        return 0;
    }
    std::memcpy(self + field::kSizeX, &x, sizeof x);
    std::memcpy(self + field::kSizeZ, &z, sizeof z);
    At<int32_t>(self, field::kCellsX) = Ftol(static_cast<double>(x) * 0.015625);
    At<int32_t>(self, field::kCellsZ) = Ftol(static_cast<double>(z) * 0.015625);
    At<int32_t>(self, field::kCityCellsX) = Ftol(static_cast<double>(x) * 0.0625);
    At<int32_t>(self, field::kCityCellsZ) = Ftol(static_cast<double>(z) * 0.0625);
    UnlockOf(self);
    return 1;
}

// 20 (0x0049EF60): false for none or for one already in (flag 1). The
// occupant is referenced and initialized; one without a property holder is
// released again. Its box, each of x and z pulled in by 0.1 when wider
// than 1, gives the bounding cells (bytes: min x, min z, max x, max z; no
// clamp), recorded in the occupant. An entry goes to the front of every
// cell's list, x then z, with bit 0 set when the rectangle is one cell;
// missing cells are made on the way. The first existing or made cell is
// searched for the occupant first: found there, nothing more is done and
// the reference is released (entries already added stay: none can have
// been, the search being at the first cell). Then flag 1 and message
// 0x99EF1142 with the caller's data, outside the lock.
uint8_t __fastcall InsertOccupant(char* self, void*, void* occupant, uint32_t data)
{
    static bool logged = false;
    FirstCall("InsertOccupant", logged);
    if (!occupant || Virtual<uint8_t>(occupant, occupant::kIsFlagSet, 1u))
        return 0;
    LockOf(self);
    AddRefOf(occupant);
    Virtual<uint8_t>(occupant, occupant::kInit);
    bool inserted = false;
    if (Virtual<void*>(occupant, occupant::kPropertyHolder)) {
        float box[6] = {};
        Virtual<void*>(occupant, occupant::kGetBoundingBox, &box[0], &box[3]);
        ShrinkAxis(box[0], box[3]);
        ShrinkAxis(box[2], box[5]);
        int32_t cellX = 0, cellZ = 0;
        Virtual<uint32_t>(self, slot::kPositionToWorldCell, box[0], box[2], &cellX, &cellZ);
        uint8_t rect[4];
        rect[0] = static_cast<uint8_t>(cellX);
        rect[1] = static_cast<uint8_t>(cellZ);
        CellBytes(self, box[3], box[5], &rect[2], &rect[3]);
        Virtual<uint8_t>(occupant, occupant::kSetCells, static_cast<uint8_t*>(rect));
        const std::uintptr_t entry =
            reinterpret_cast<std::uintptr_t>(occupant) + (rect[2] <= rect[0] && rect[3] <= rect[1] ? 1u : 0u);
        bool search = true;
        inserted = true;
        for (int32_t x = rect[0]; inserted && x <= rect[2]; ++x)
            for (int32_t z = rect[1]; z <= rect[3]; ++z) {
                Cell* cell = CellAt(self, x, z);
                if (!cell) {
                    cell = NewCell();
                    CellAt(self, x, z) = cell;
                    if (!cell)
                        continue;
                }
                if (search) {
                    search = false;
                    if (FindNode(cell, occupant) != cell->head) {
                        inserted = false;
                        break;
                    }
                }
                InsertFront(cell, entry);
            }
    }
    UnlockOf(self);
    if (!inserted) {
        ReleaseOf(occupant);
        return 0;
    }
    Virtual<uint32_t>(occupant, occupant::kSetFlag, 1u);
    Virtual<uint8_t>(occupant, occupant::kPostMessage, kMessageInserted, data);
    return 1;
}

// A cell's entry for the occupant erased, when it has one; an emptied cell
// is freed (the list through 0x005650A0, the cell through operator delete)
// and its grid entry cleared.
void EraseFrom(char* self, int32_t x, int32_t z, Cell* cell, const void* occupant)
{
    ListNode* const node = FindNode(cell, occupant);
    if (node == cell->head)
        return;
    EraseNode(node);
    if (cell->head->next != cell->head)
        return;
    DestroyList(cell);
    OperatorDelete(cell);
    CellAt(self, x, z) = nullptr;
}

// 21 (0x0049F240): the occupant is referenced for the call. One that is in
// (flag 1), has a property holder and recorded cells leaves every cell of
// its rectangle; then message 0x99EF1143 with the caller's data, flag 1
// cleared and the manager's reference released, outside the lock. Whether
// removed or not, its Shutdown when asked, and the call's reference
// released. Returns whether it was removed.
uint8_t __fastcall RemoveOccupant(char* self, void*, void* occupant, uint32_t shutdown, uint32_t data)
{
    static bool logged = false;
    FirstCall("RemoveOccupant", logged);
    bool removed = false;
    if (occupant) {
        AddRefOf(occupant);
        if (Virtual<uint8_t>(occupant, occupant::kIsFlagSet, 1u)) {
            LockOf(self);
            uint8_t rect[4] = {};
            if (Virtual<void*>(occupant, occupant::kPropertyHolder) &&
                Virtual<uint8_t>(occupant, occupant::kGetCells, static_cast<uint8_t*>(rect))) {
                for (int32_t x = rect[0]; x <= rect[2]; ++x)
                    for (int32_t z = rect[1]; z <= rect[3]; ++z)
                        if (Cell* const cell = CellAt(self, x, z))
                            EraseFrom(self, x, z, cell, occupant);
                removed = true;
            }
            UnlockOf(self);
            if (removed) {
                Virtual<uint8_t>(occupant, occupant::kPostMessage, kMessageRemoved, data);
                Virtual<uint32_t>(occupant, occupant::kClearFlag, 1u);
                ReleaseOf(occupant);
            }
        }
        if (shutdown & 0xFF)
            Virtual<uint8_t>(occupant, occupant::kShutdown);
        ReleaseOf(occupant);
    }
    return removed ? 1 : 0;
}

// The collecting vector of RemoveOccupants: a push_back that grows through
// the allocator service cached at 0x00B21750 (double, at least one; the
// old buffer freed through it, null included).
void PushFound(Vector* found, void* occupant)
{
    if (found->end != found->capacity) {
        *reinterpret_cast<void**>(found->end) = occupant;
        found->end += 4;
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(found->end - found->begin);
    const uint32_t count = static_cast<uint32_t>(bytes >> 2);
    const uint32_t capacity = count + (count < 1 ? 1 : count);
    char* const fresh = Virtual<char*>(NodeAllocatorAt(addr::SC4_PointerArrayAllocator), 0xC, capacity * 4);
    char* at = fresh;
    if (found->end != found->begin) {
        std::memmove(fresh, found->begin, bytes);
        at = fresh + bytes;
    }
    *reinterpret_cast<void**>(at) = occupant;
    Virtual<void>(NodeAllocatorAt(addr::SC4_PointerArrayAllocator), 0x10, static_cast<void*>(found->begin));
    found->begin = fresh;
    found->end = at + 4;
    found->capacity = fresh + capacity * 4;
}

// 22 (0x0049FCD0): every world cell, x then z. A cell's occupants that the
// filter takes (all without one) are collected front to back; then each
// that has a property holder and recorded cells leaves every cell of its
// rectangle (so an occupant of several cells goes with the first of them
// met), joins the removed list and has flag 1 cleared. After the lock, in
// that order: message 0x99EF1143 with the caller's data, Shutdown when
// asked, the manager's reference released. No initialized test. True.
uint8_t __fastcall RemoveOccupants(char* self, void*, void* filter, uint32_t shutdown, uint32_t data)
{
    static bool logged = false;
    FirstCall("RemoveOccupants", logged);
    ListNode* removed = NewNodeList();
    Vector found{nullptr, nullptr, nullptr};
    LockOf(self);
    for (int32_t x = 0; x < kWorldCells; ++x)
        for (int32_t z = 0; z < kWorldCells; ++z) {
            const Cell* const cell = CellAt(self, x, z);
            if (!cell)
                continue;
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (filter && !Virtual<uint8_t>(filter, 0xC, occupant))
                    continue;
                PushFound(&found, occupant);
            }
            for (void** it = reinterpret_cast<void**>(found.begin); it != reinterpret_cast<void**>(found.end); ++it) {
                void* const occupant = *it;
                uint8_t rect[4] = {};
                if (!Virtual<void*>(occupant, occupant::kPropertyHolder) ||
                    !Virtual<uint8_t>(occupant, occupant::kGetCells, static_cast<uint8_t*>(rect)))
                    continue;
                for (int32_t cellX = rect[0]; cellX <= rect[2]; ++cellX)
                    for (int32_t cellZ = rect[1]; cellZ <= rect[3]; ++cellZ) {
                        Cell* const other = CellAt(self, cellX, cellZ);
                        if (!other)
                            continue;
                        ListNode* const node = FindNode(other, occupant);
                        if (node == other->head)
                            continue;
                        EraseNode(node);
                        if (other->head->next != other->head)
                            continue;
                        // The game's inline list destructor: nothing left
                        // to free but the head node.
                        other->head->next = other->head;
                        other->head->previous = other->head;
                        if (other->head)
                            Free(other->head);
                        OperatorDelete(other);
                        CellAt(self, cellX, cellZ) = nullptr;
                    }
                AppendNode(removed, occupant);
                Virtual<uint32_t>(occupant, occupant::kClearFlag, 1u);
            }
            found.end = found.begin;
        }
    UnlockOf(self);
    for (ListNode* node = removed->next; node != removed; node = node->next) {
        void* const occupant = reinterpret_cast<void*>(node->value);
        Virtual<uint8_t>(occupant, occupant::kPostMessage, kMessageRemoved, data);
        if (shutdown & 0xFF)
            Virtual<uint8_t>(occupant, occupant::kShutdown);
        ReleaseOf(occupant);
    }
    if (found.begin)
        Virtual<void>(NodeAllocatorAt(addr::SC4_PointerArrayAllocator), 0x10, static_cast<void*>(found.begin));
    ClearNodeList(&removed);
    Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(removed));
    return 1;
}

// 23 (0x0049DB80): the type put in the manager's own filter, then slot 22
// with it (the game's tail jump). No initialized test: before Init there
// is no filter.
uint32_t __fastcall RemoveOccupantsByType(char* self, void*, uint32_t type, uint32_t shutdown, uint32_t data)
{
    static bool logged = false;
    FirstCall("RemoveOccupantsByType", logged);
    At<uint32_t>(At<void*>(self, field::kFilter), 0xC) = type;
    return Virtual<uint32_t>(self, slot::kRemoveOccupants, At<void*>(self, field::kFilter), shutdown, data);
}

// 24 (0x0049F3C0): false for none, for one not in (flag 1), without a
// property holder or without recorded cells. The bounding cells are taken
// again as InsertOccupant does and recorded. When they changed: the entry
// leaves the old cells outside the new rectangle (their lists must exist:
// the game does not test), a new entry goes to the front of the new cells
// outside the old rectangle, and, when the one-cell bit changed, the
// entries in the cells of both get the new value. Message 0x99EF1146 (data
// 0) when asked, outside the lock. True also when nothing changed.
uint8_t __fastcall MoveOccupant(char* self, void*, void* occupant, uint32_t notify)
{
    static bool logged = false;
    FirstCall("MoveOccupant", logged);
    if (!occupant || !Virtual<uint8_t>(occupant, occupant::kIsFlagSet, 1u))
        return 0;
    LockOf(self);
    bool moved = false;
    uint8_t old[4] = {};
    if (Virtual<void*>(occupant, occupant::kPropertyHolder) &&
        Virtual<uint8_t>(occupant, occupant::kGetCells, static_cast<uint8_t*>(old))) {
        float box[6] = {};
        Virtual<void*>(occupant, occupant::kGetBoundingBox, &box[0], &box[3]);
        ShrinkAxis(box[0], box[3]);
        ShrinkAxis(box[2], box[5]);
        uint8_t now[4];
        CellBytes(self, box[0], box[2], &now[0], &now[1]);
        CellBytes(self, box[3], box[5], &now[2], &now[3]);
        Virtual<uint8_t>(occupant, occupant::kSetCells, static_cast<uint8_t*>(now));
        if (std::memcmp(old, now, 4) != 0) {
            const std::uintptr_t pointer = reinterpret_cast<std::uintptr_t>(occupant);
            const std::uintptr_t oldEntry = pointer + (old[2] <= old[0] && old[3] <= old[1] ? 1u : 0u);
            for (int32_t x = old[0]; x <= old[2]; ++x)
                for (int32_t z = old[1]; z <= old[3]; ++z) {
                    if (x >= now[0] && x <= now[2] && z >= now[1] && z <= now[3])
                        continue;
                    EraseFrom(self, x, z, CellAt(self, x, z), occupant);
                }
            const std::uintptr_t newEntry = pointer + (now[2] <= now[0] && now[3] <= now[1] ? 1u : 0u);
            for (int32_t x = now[0]; x <= now[2]; ++x)
                for (int32_t z = now[1]; z <= now[3]; ++z) {
                    if (x >= old[0] && x <= old[2] && z >= old[1] && z <= old[3])
                        continue;
                    InsertFront(GetCell(self, x, z, true), newEntry);
                }
            if (oldEntry != newEntry) {
                const int32_t x0 = old[0] < now[0] ? now[0] : old[0];
                const int32_t z0 = old[1] < now[1] ? now[1] : old[1];
                const int32_t x1 = now[2] < old[2] ? now[2] : old[2];
                const int32_t z1 = now[3] < old[3] ? now[3] : old[3];
                for (int32_t x = x0; x <= x1; ++x)
                    for (int32_t z = z0; z <= z1; ++z) {
                        const Cell* const cell = GetCell(self, x, z, true);
                        ListNode* const node = FindNode(cell, occupant);
                        if (node != cell->head)
                            node->value = newEntry;
                    }
            }
        }
        moved = true;
    }
    UnlockOf(self);
    if (moved && (notify & 0xFF))
        Virtual<uint8_t>(occupant, occupant::kPostMessage, kMessageMoved, 0u);
    return moved ? 1 : 0;
}

// 27 (0x0049FB30): whether the occupant is in the list of its first cell
// (FindCell); x and z are NOT written then. Otherwise, when asked, every
// cell is searched, x then z, and the cell where it is found written
// (each when given). No initialized test.
uint8_t __fastcall FindOccupant(char* self, void*, void* occupant, uint32_t searchAll, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("FindOccupant", logged);
    LockOf(self);
    int32_t firstX = 0, firstZ = 0;
    bool found = false;
    if (const Cell* const cell = FindCell(self, occupant, &firstX, &firstZ))
        found = FindNode(cell, occupant) != cell->head;
    if (!found && (searchAll & 0xFF)) {
        for (int32_t x = 0; !found && x < kWorldCells; ++x)
            for (int32_t z = 0; z < kWorldCells; ++z) {
                const Cell* const cell = CellAt(self, x, z);
                if (!cell || FindNode(cell, occupant) == cell->head)
                    continue;
                if (outX)
                    *outX = x;
                if (outZ)
                    *outZ = z;
                found = true;
                break;
            }
    }
    UnlockOf(self);
    return found ? 1 : 0;
}

#define OCCMGR_SLOT(index, name)                                                                                     \
    {                                                                                                                \
        addr::cSC4OccupantManager_vftable, index, addr::cSC4OccupantManager_##name, reinterpret_cast<void*>(&name)  \
    }

const Slot kEditSlots[] = {
    OCCMGR_SLOT(5, SetCitySize),
    OCCMGR_SLOT(20, InsertOccupant),
    OCCMGR_SLOT(21, RemoveOccupant),
    OCCMGR_SLOT(22, RemoveOccupants),
    OCCMGR_SLOT(23, RemoveOccupantsByType),
    OCCMGR_SLOT(24, MoveOccupant),
    OCCMGR_SLOT(27, FindOccupant),
};

bool InstallEdit(registry::Mode) { return SwapSlots(kEditSlots, sizeof kEditSlots / sizeof kEditSlots[0], "edit"); }

const bool g_registered = registry::Register(
    {"simulators.occmgr.edit", "simulators/occmgr", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4OccupantManager_InsertOccupant,
     "Our occupant manager edits: SetCitySize, InsertOccupant, RemoveOccupant, RemoveOccupants, MoveOccupant and "
     "FindOccupant (src/core/occmgr_edit.cpp)",
     false, InstallEdit});

} // namespace
} // namespace opensc4::occmgr_sim
