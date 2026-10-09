// Our cSC4TrafficNetworkMap life cycle, cell records and messages (build 641;
// docs/traffic-map.md): Init/Shutdown (simulators.trafficmap.init), the
// occupant slots 47-50 with their helpers (simulators.trafficmap.occupants)
// and DoMessage of the message-target part (simulators.trafficmap.messages).
//
// Every call into the game, allocation and free happens in the game's order
// with the game's arguments; the STL template code (hash insert/erase/clear,
// vector copy/growth) stays the game's and is called by address.
#include "core/trafficmap.h"

#include "core/game.h"
#include "core/game_access.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>

namespace opensc4::trafficmap {
namespace {

// An iterator of the game's hash tables: the node and its table.
struct Iterator {
    void* node;
    Hashtable* table;
};

// What the game's insert_unique_noresize writes: the node, the table, inserted.
struct InsertResult {
    void* node;
    Hashtable* table;
    uint8_t inserted;
    uint8_t pad[3];
};

// The value part of a SignalNode (node + 8), as the game builds it on the stack.
struct SignalValue {
    Approach approaches[4];
    uint8_t byte58;
    uint8_t pad59[3];
    GameVector objects;
    ListNode* waiting;
    uint8_t changed;
    uint8_t pad6D[3];
};
static_assert(sizeof(SignalValue) == 0x68);

struct SignalPair {
    uint32_t key;
    SignalValue value;
};

struct LanePair {
    uint32_t key;
    LaneWay ways[4];
};
static_assert(sizeof(LanePair) == 0x44);

struct CellObjectsPair {
    uint32_t key;
    GameVector objects;
};

using VoidFn = void(__thiscall*)(void* self);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using PoolBlockFn = void(__thiscall*)(void* pool, void* block);
using PoolGrowFn = CellRecord*(__thiscall*)(void* pool);
using TableFn = void(__thiscall*)(Hashtable* table);
using IteratorFn = void(__thiscall*)(Hashtable* table, Iterator* it);
using EraseByValueFn = void(__thiscall*)(Hashtable* table, void* node, Hashtable* itTable);
using InsertFn = void(__thiscall*)(Hashtable* table, InsertResult* out, const void* pair);
using ResizeFn = void(__thiscall*)(Hashtable* table, uint32_t count);
using FindFn = void(__thiscall*)(Hashtable* table, Iterator* out, const uint32_t* key);
using EraseKeyFn = uint32_t(__thiscall*)(Hashtable* table, const uint32_t* key);
using CopyFn = void*(__thiscall*)(void* self, const void* other);
using FindPointerFn = void**(__cdecl*)(void** first, void** last, void* const* value, void* const* tag);
using PushBackFn = void(__thiscall*)(GameVector* vector, void* const* value);
using EraseFn = void**(__thiscall*)(GameVector* vector, void** item);
using ListDestroyFn = void(__thiscall*)(ListNode** list);
using ServiceFn = void*(__cdecl*)();
using DistanceFn = double(__cdecl*)(const float* a, const float* b);

// The messages the map listens to (Init registers them in this order).
constexpr uint32_t kMessages[] = {0xA6B79602u, 0xA6B79612u, 0x99EF1142u, 0x89D3E9F3u, 0x99EF1143u,
                                  0x69247DC7u, 0x26D31EC1u, 0xCAD61E00u, 0xEAD61E16u};

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }
inline void* Allocate(std::size_t size) { return reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size); }
inline void Free(void* p) { reinterpret_cast<FreeFn>(addr::SC4_Free)(p); }
inline void* MessageTarget(void* self) { return static_cast<char*>(self) + 8; }
inline uint32_t Key(const void* p) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// The cells an occupant covers: x0, z0, x1, z1 (its slot 0xC0 object's slot 0x2C).
void OccupantRect(void* occupant, int32_t* rect)
{
    void* const shape = Virtual<void*>(occupant, 0xC0);
    Virtual<void>(shape, 0x2C, rect);
}

// The cell-object key of a cell: x << 16 | z.
inline uint32_t CellKey(const int32_t* rect)
{
    return static_cast<uint32_t>(rect[0]) << 16 | static_cast<uint32_t>(rect[1]);
}

// A cell record back to its pool page (as the game inlines it).
void ReturnToPool(CellRecord* record)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(record) & 0xFFFFE000u);
    At<void*>(record, 0) = At<void*>(page, 8);
    const uint32_t left = At<uint32_t>(page, 0xC) - 1;
    At<CellRecord*>(page, 8) = record;
    At<uint32_t>(page, 0xC) = left;
    void* const pool = reinterpret_cast<void*>(addr::SC4_CellRecordPool);
    if (left == 0)
        reinterpret_cast<PoolBlockFn>(addr::SC4Pool_ReleaseBlock)(pool, page);
    else if (left == 0x40000045u)
        reinterpret_cast<PoolBlockFn>(addr::SC4Pool_ReopenBlock)(pool, page);
}

// Frees every node of a game list but the sentinel and empties it.
void ClearList(ListNode* const& sentinel)
{
    for (ListNode* node = sentinel->next; node != sentinel;) {
        ListNode* const done = node;
        node = node->next;
        Free(done);
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

// 0x004AFB10: releases a record's occupant and automata and empties its automata list.
void ReleaseRecord(CellRecord* record)
{
    if (!record)
        return;
    if (record->occupant)
        Release(record->occupant);
    for (ListNode* node = record->automata->next; node != record->automata; node = node->next)
        if (node->value)
            Release(node->value);
    ClearList(record->automata);
}

// The rest of a record's teardown after 0x004AFB10 (inlined by 0x004B3B40 and
// slot 49): the list, the vectors, back to the pool.
void DestroyRecord(CellRecord* record)
{
    ClearList(record->automata);
    if (record->automata)
        Free(record->automata);
    if (record->paths.begin)
        Free(record->paths.begin);
    auto* const lanesEnd = static_cast<GameVector*>(record->lanes.end);
    for (auto* lane = static_cast<GameVector*>(record->lanes.begin); lane != lanesEnd; ++lane)
        if (lane->begin)
            Free(lane->begin);
    if (record->lanes.begin)
        Free(record->lanes.begin);
    ReturnToPool(record);
}

// 0x004B19A0: releases the objects of a vector and empties it.
void ReleaseObjects(GameVector* objects)
{
    for (auto** it = static_cast<void**>(objects->begin); it != objects->end; ++it)
        if (*it)
            Release(*it);
    objects->end = objects->begin;
}

// ---------------------------------------------------------------- init

// 0x004ADF50: tunnels again: every occupant with flag 0x80000 and interface
// 0x8A4BD52B hands itself and that interface's +0x1D0 to its network object.
void RegisterTunnels(void* self)
{
    const int32_t count = At<int32_t>(self, field::kHeight) * At<int32_t>(self, field::kWidth);
    void* tunnel = nullptr;  // one stack slot for the whole walk, as in the game
    for (int32_t i = 0; i < count; ++i) {
        for (CellRecord* record = At<CellRecord**>(self, field::kGrid)[i]; record; record = record->next) {
            void* const occupant = record->occupant;
            if (!occupant || !Virtual<uint8_t>(occupant, 0x20, 0x80000u) ||
                !Virtual<uint8_t>(occupant, 0x0, 0x8A4BD52Bu, &tunnel))
                continue;
            void* const network = Virtual<void*>(occupant, 0xC4);
            void* const other = At<void*>(tunnel, 0x1D0);  // the getter 0x00647570
            if (network && other)
                Virtual<void>(network, 0x80, occupant, other);
            Release(occupant);
        }
    }
}

// 0x004B3B40: releases and frees every cell record, then the grid.
void FreeGrid(void* self)
{
    const int32_t count = At<int32_t>(self, field::kHeight) * At<int32_t>(self, field::kWidth);
    for (int32_t i = 0; i < count; ++i) {
        CellRecord* record = At<CellRecord**>(self, field::kGrid)[i];
        while (record) {
            CellRecord* const next = record->next;
            ReleaseRecord(record);
            DestroyRecord(record);
            record = next;
        }
    }
    reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(At<void*>(self, field::kGrid));
    At<void*>(self, field::kGrid) = nullptr;
}

// 0x004B36E0: releases each signal's occupant and objects, then clears the table.
void ClearSignals(void* self)
{
    Hashtable& table = Table(self, field::kSignals);
    for (auto* node = First<SignalNode>(table); node; node = Next(table, node)) {
        if (node->key)
            Release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)));
        for (auto** it = static_cast<void**>(node->objects.begin); it != node->objects.end; ++it)
            if (*it)
                Release(*it);
        node->objects.end = node->objects.begin;
    }
    reinterpret_cast<TableFn>(addr::SC4SignalTable_Clear)(&table);
}

// 0x004B3CE0: releases each lane node's occupant, then clears the table.
void ClearLanes(void* self)
{
    Hashtable& table = Table(self, field::kLanes);
    for (auto* node = First<LaneNode>(table); node; node = Next(table, node))
        if (node->key)
            Release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)));
    reinterpret_cast<TableFn>(addr::SC4LaneTable_Clear)(&table);
}

// 0x004B3580: releases every object waiting on a cell, then clears the table.
void ClearCellObjects(void* self)
{
    Hashtable& table = Table(self, field::kCellObjects);
    for (auto* node = First<CellObjectsNode>(table); node; node = Next(table, node))
        for (auto** it = static_cast<void**>(node->objects.begin); it != node->objects.end; ++it)
            Release(*it);
    reinterpret_cast<TableFn>(addr::SC4PtrVectorHashMap_Clear)(&table);
}

// A new occupant filter (0xC bytes) with the given vtables, or null.
void* NewFilter(uint32_t vtable, uint32_t vtable4)
{
    void* const filter = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0xC);
    if (!filter)
        return nullptr;
    reinterpret_cast<VoidFn>(addr::SC4_OccupantFilterBase)(filter);
    At<uint32_t>(filter, 0) = vtable;
    At<uint32_t>(filter, 4) = vtable4;
    return filter;
}

// 3 (0x004AE4E0): once: the city's size, the grid, the view and occupant
// manager, the two occupant filters, the messages, the 0x86AD10EC service.
uint8_t __fastcall Init(void* self, void* /*edx*/)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    At<int32_t>(self, field::kWidth) = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x174);
    const int32_t height = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178);
    At<int32_t>(self, field::kHeight) = height;
    const int32_t width = At<int32_t>(self, field::kWidth);
    At<uint8_t>(self, field::kShift) = 1;
    for (;;) {
        uint8_t shift = At<uint8_t>(self, field::kShift);
        if ((1u << shift) == static_cast<uint32_t>(width))
            break;
        ++shift;
        At<uint8_t>(self, field::kShift) = shift;
        if (shift >= 0x20)
            break;
    }
    if (!At<void*>(self, field::kGrid)) {
        const int32_t count = width * height;
        At<void*>(self, field::kGrid) =
            reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(static_cast<std::size_t>(count) * 4);
        for (int32_t i = 0; i < count; ++i)
            At<CellRecord**>(self, field::kGrid)[i] = nullptr;
    }
    if (!At<void*>(self, field::kView))
        At<void*>(self, field::kView) = Global<void*>(addr::SC4_CameraView);
    if (!At<void*>(self, field::kOccupantManager)) {
        void* const manager = Global<void*>(addr::SC4_NetworkService);
        At<void*>(self, field::kOccupantManager) = manager;
        AddRef(manager);
    }
    if (!At<void*>(self, field::kFilterA)) {
        void* const filter = NewFilter(0x00A8FE14u, 0x00A8FDFCu);
        At<void*>(self, field::kFilterA) = filter;
        AddRef(filter);
    }
    if (!At<void*>(self, field::kFilterB)) {
        void* const filter = NewFilter(0x00A8FE48u, 0x00A8FE30u);
        At<void*>(self, field::kFilterB) = filter;
        AddRef(filter);
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, MessageTarget(self), message);
    if (void* const service = reinterpret_cast<ServiceFn>(addr::SC4_GetService86AD10EC)())
        Virtual<uint8_t>(service, 0x14, MessageTarget(self), 0xC0u);
    if (At<uint8_t>(self, field::kFlag25))
        RegisterTunnels(self);
    return 1;
}

// 4 (0x004B4000): frees the grid and the tables, unregisters, releases the
// filters and the occupant manager.
uint8_t __fastcall Shutdown(void* self, void* /*edx*/)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    FreeGrid(self);
    ClearSignals(self);
    ClearLanes(self);
    ClearCellObjects(self);
    if (void* const service = reinterpret_cast<ServiceFn>(addr::SC4_GetService86AD10EC)())
        Virtual<uint8_t>(service, 0x18, MessageTarget(self), 0xC0u);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, MessageTarget(self), message);
    At<void*>(self, field::kView) = nullptr;
    for (const std::size_t offset : {field::kFilterA, field::kFilterB, field::kOccupantManager}) {
        if (void* const held = At<void*>(self, offset)) {
            Release(held);
            At<void*>(self, offset) = nullptr;
        }
    }
    At<uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// ---------------------------------------------------------------- occupants

// 0x004B34A0: takes an object off its cell's list (the cell from `rect`, or
// from the object when null), and the cell's node when it empties.
void RemoveCellObject(void* self, void* object, const int32_t* rect)
{
    if (!object)
        return;
    uint32_t key;
    if (!rect) {
        int32_t own[4];
        OccupantRect(object, own);
        key = CellKey(own);
    } else {
        key = CellKey(rect);
    }
    Hashtable& table = Table(self, field::kCellObjects);
    auto* const node = Find<CellObjectsNode>(table, key);
    if (!node)
        return;
    void* const value = object;  // the argument slot the game passes by address
    auto** const end = static_cast<void**>(node->objects.end);
    void** const it = reinterpret_cast<FindPointerFn>(addr::SC4_FindPointer)(static_cast<void**>(node->objects.begin),
                                                                            end, &value, &value);
    if (it == end)
        return;
    reinterpret_cast<EraseFn>(addr::SC4PtrVector_Erase)(&node->objects, it);
    Release(object);
    if (node->objects.begin == node->objects.end)
        reinterpret_cast<EraseByValueFn>(addr::SC4PtrVectorHashMap_EraseAt)(&table, node, &table);
}

// 0x004B3620: releases and erases an occupant's signal node.
void RemoveSignal(void* self, void* occupant)
{
    Hashtable& table = Table(self, field::kSignals);
    auto* const node = Find<SignalNode>(table, Key(occupant));
    if (!node)
        return;
    Release(occupant);
    ReleaseObjects(&node->objects);
    ListNode* const waiting = node->waiting;
    for (ListNode* item = waiting->next; item != waiting; item = item->next)
        Release(item->value);
    ClearList(node->waiting);
    Iterator it{node, &table};
    reinterpret_cast<IteratorFn>(addr::SC4SignalTable_EraseAt)(&table, &it);
}

// 0x004B3C80: releases and erases an occupant's lane node.
void RemoveLanes(void* self, void* occupant)
{
    Hashtable& table = Table(self, field::kLanes);
    auto* const node = Find<LaneNode>(table, Key(occupant));
    if (!node)
        return;
    Release(occupant);
    Iterator it{node, &table};
    reinterpret_cast<IteratorFn>(addr::SC4LaneTable_EraseAt)(&table, &it);
}

// 0x004B4300: a signal node for an occupant whose network object is of type 1
// or 3, then the signals of the four neighbouring cells set up again.
void AddSignal(void* self, void* occupant)
{
    if (!occupant)
        return;
    Hashtable& table = Table(self, field::kSignals);
    void* const network = Virtual<void*>(occupant, 0xC4);
    if (network && (Virtual<uint8_t>(network, 0x34, 1u) || Virtual<uint8_t>(network, 0x34, 3u))) {
        // The game leaves the approaches and the two bytes of this stack value
        // uninitialised (SetUpSignal fills them); ours are zero.
        SignalValue value{};
        value.objects = GameVector{nullptr, nullptr, nullptr};
        auto* const sentinel = static_cast<ListNode*>(Allocate(0xC));
        sentinel->next = sentinel;
        sentinel->prev = sentinel;
        value.waiting = sentinel;
        SignalPair pair;
        pair.key = Key(occupant);
        reinterpret_cast<CopyFn>(addr::SC4SignalValue_Copy)(&pair.value, &value);
        reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(&table, table.count + 1);
        InsertResult result;
        reinterpret_cast<InsertFn>(addr::SC4SignalTable_InsertUnique)(&table, &result, &pair);
        reinterpret_cast<ListDestroyFn>(addr::SC4PairList_Destroy)(&pair.value.waiting);
        if (pair.value.objects.begin)
            Free(pair.value.objects.begin);
        reinterpret_cast<ListDestroyFn>(addr::SC4PairList_Destroy)(&value.waiting);
        if (value.objects.begin)
            Free(value.objects.begin);
        if (result.inserted) {
            AddRef(occupant);
            SetUpSignal(self, occupant, static_cast<SignalNode*>(result.node));
        }
    }
    int32_t rect[4];
    OccupantRect(occupant, rect);
    const int16_t* const offsets = &Global<int16_t>(addr::cSC4TrafficNetworkMap_NeighbourOffsets);
    for (int i = 0; i < 4; ++i) {
        auto* record = Virtual<CellRecord*>(self, 0x38, rect[0] + offsets[2 * i], rect[1] + offsets[2 * i + 1]);
        for (; record; record = record->next) {
            void* const other = record->occupant;
            if (auto* const node = Find<SignalNode>(table, Key(other)))
                SetUpSignal(self, other, node);
        }
    }
}

// 0x004B44F0: the occupant's lane node replaced by the ways its network object
// gives for travel types 3 or 7 in each direction (no node when none does).
void AddLanes(void* self, void* occupant)
{
    if (!occupant)
        return;
    AddRef(occupant);
    void* const network = Virtual<void*>(occupant, 0xC4);
    if (!network) {
        Release(occupant);
        return;
    }
    Hashtable* const table = &Table(self, field::kLanes);
    const uint32_t key = Key(occupant);
    Iterator found;
    reinterpret_cast<FindFn>(addr::SC4Uint32HashTable_Find)(table, &found, &key);
    if (found.node) {
        reinterpret_cast<EraseKeyFn>(addr::SC4LaneTable_EraseKey)(table, &key);
        Release(occupant);
    }
    // The game leaves the indexes of ways without lanes uninitialised; ours are zero.
    LaneWay ways[4] = {};
    uint8_t none = 1;
    for (uint8_t i = 0; i < 4; ++i) {
        // The game keeps the direction in the low byte of the found iterator's
        // node slot and passes the whole dword.
        const uint32_t direction = (Key(found.node) & 0xFFFFFF00u) | i;
        if (Virtual<uint8_t>(network, 0x98, 3u, direction, &ways[i]) ||
            Virtual<uint8_t>(network, 0x98, 7u, direction, &ways[i])) {
            ways[i].index = 0;
            none = 0;
        }
    }
    if (!none) {
        LanePair pair;
        pair.key = key;
        reinterpret_cast<CopyFn>(addr::SC4LaneValue_Copy)(&pair.ways, ways);
        reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(table, table->count + 1);
        InsertResult result;
        reinterpret_cast<InsertFn>(addr::SC4LaneTable_InsertUnique)(table, &result, &pair);
        for (int i = 3; i >= 0; --i)
            if (pair.ways[i].values.begin)
                Free(pair.ways[i].values.begin);
        if (result.inserted)
            AddRef(occupant);
    }
    for (int i = 3; i >= 0; --i)
        if (ways[i].values.begin)
            Free(ways[i].values.begin);
    Release(occupant);
}

// 47 (0x004B3380): adds a referenced object to its cell's list; false if the
// cell's node could not be inserted.
uint8_t __fastcall AddCellObject(void* self, void* /*edx*/, void* object)
{
    static bool logged = false;
    FirstCall("AddCellObject", logged);
    if (!object)
        return 1;
    int32_t rect[4];
    OccupantRect(object, rect);
    const uint32_t key = CellKey(rect);
    Hashtable& table = Table(self, field::kCellObjects);
    auto* node = Find<CellObjectsNode>(table, key);
    if (!node) {
        const GameVector empty{nullptr, nullptr, nullptr};
        CellObjectsPair pair;
        pair.key = key;
        reinterpret_cast<CopyFn>(addr::SC4PtrVector_Copy)(&pair.objects, &empty);
        reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(&table, table.count + 1);
        InsertResult result;
        reinterpret_cast<InsertFn>(addr::SC4PtrVectorHashMap_InsertUnique)(&table, &result, &pair);
        if (pair.objects.begin)
            Free(pair.objects.begin);
        if (!result.inserted)
            return 0;
        node = static_cast<CellObjectsNode*>(result.node);
        if (!node)
            return 1;
    }
    void* const value = object;  // the argument slot the game passes by address
    auto** const end = static_cast<void**>(node->objects.end);
    if (reinterpret_cast<FindPointerFn>(addr::SC4_FindPointer)(static_cast<void**>(node->objects.begin), end, &value,
                                                              &value) == end) {
        AddRef(object);
        reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack)(&node->objects, &value);
    }
    return 1;
}

// 48 (0x004B4710): one cell record per covered cell; with the flag, the
// occupant comes off the cell-object lists; signal and lanes if filter A takes it.
uint8_t __fastcall AddOccupant(void* self, void* /*edx*/, void* occupant, uint32_t fromCellObjects)
{
    static bool logged = false;
    FirstCall("AddOccupant", logged);
    if (!occupant)
        return 1;
    AddRef(occupant);
    int32_t rect[4];
    OccupantRect(occupant, rect);
    void* const pool = reinterpret_cast<void*>(addr::SC4_CellRecordPool);
    char*& page = Global<char*>(addr::SC4_CellRecordPool + 4);
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            auto* record = At<CellRecord*>(page, 8);
            if (!record)
                record = reinterpret_cast<PoolGrowFn>(addr::SC4EdgePool_Grow)(pool);
            At<void*>(page, 8) = At<void*>(record, 0);
            ++At<uint32_t>(page, 0xC);
            record->lanes = GameVector{nullptr, nullptr, nullptr};
            record->paths = GameVector{nullptr, nullptr, nullptr};
            record->automata = nullptr;
            auto* const sentinel = static_cast<ListNode*>(Allocate(0xC));
            sentinel->next = sentinel;
            sentinel->prev = sentinel;
            record->automata = sentinel;
            record->occupant = occupant;
            AddRef(occupant);
            record->next = nullptr;
            record->flags20 = 0;
            record->flags24 = 0;
            record->flags28 = 0;
            for (int i = 0; i < 4; ++i) {
                record->approachTraffic[i] = 0;
                for (int j = 0; j < 4; ++j)
                    record->turnTraffic[i][j] = 0;
            }
            CellRecord*& head = CellAt(self, x, z);
            record->next = head;
            head = record;
        }
    }
    if (static_cast<uint8_t>(fromCellObjects))
        RemoveCellObject(self, occupant, rect);
    void* const filter = At<void*>(self, field::kFilterA);
    if (Virtual<uint8_t>(filter, 0xC, Virtual<void*>(occupant, 0xC0))) {
        AddSignal(self, occupant);
        AddLanes(self, occupant);
    }
    Release(occupant);
    return 1;
}

// 49 (0x004B4120): drops the occupant's signal, lanes, cell records and
// cell-object entry.
uint8_t __fastcall RemoveOccupant(void* self, void* /*edx*/, void* occupant)
{
    static bool logged = false;
    FirstCall("RemoveOccupant", logged);
    RemoveSignal(self, occupant);
    RemoveLanes(self, occupant);
    int32_t rect[4];
    OccupantRect(occupant, rect);
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            CellRecord** link = &CellAt(self, x, z);
            while (CellRecord* const record = *link) {
                if (record->occupant != occupant) {
                    link = &record->next;
                    continue;
                }
                *link = record->next;
                ReleaseRecord(record);
                DestroyRecord(record);
            }
        }
    }
    RemoveCellObject(self, occupant, rect);
    return 1;
}

// 50 (0x004B48B0), only with +0x25: the cell objects become occupants, every
// signal's objects are refreshed, the lanes rebuilt and the occupants with
// flag 0x30 removed.
uint8_t __fastcall Rebuild(void* self, void* /*edx*/)
{
    static bool logged = false;
    FirstCall("Rebuild", logged);
    if (!At<uint8_t>(self, field::kFlag25))
        return 1;
    Hashtable& objects = Table(self, field::kCellObjects);
    for (auto* node = First<CellObjectsNode>(objects); node; node = Next(objects, node)) {
        for (auto** it = static_cast<void**>(node->objects.begin); it != node->objects.end; ++it) {
            void* const object = *it;
            Virtual<void>(object, 0x2C, 0x4000u);
            Virtual<void>(object, 0x28, 0x10000000u);
            Release(object);
            Virtual<uint8_t>(self, 0xC0, object, 0u);
        }
    }
    reinterpret_cast<TableFn>(addr::SC4PtrVectorHashMap_Clear)(&objects);
    Hashtable& signals = Table(self, field::kSignals);
    for (auto* node = First<SignalNode>(signals); node; node = Next(signals, node))
        RefreshSignalObjects(self, reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)), node);
    auto* const removals = static_cast<ListNode*>(Allocate(0xC));
    removals->next = removals;
    removals->prev = removals;
    const uint32_t count =
        static_cast<uint32_t>(At<int32_t>(self, field::kHeight) * At<int32_t>(self, field::kWidth));
    for (uint32_t i = 0; i < count; ++i) {
        for (CellRecord* record = At<CellRecord**>(self, field::kGrid)[i]; record; record = record->next) {
            if (!record->occupant)
                continue;
            AddLanes(self, record->occupant);
            void* const occupant = record->occupant;
            if (!Virtual<uint8_t>(occupant, 0x24, 0x30u))
                continue;
            auto* const item = static_cast<ListNode*>(Allocate(0xC));
            item->value = occupant;
            ListNode* const tail = removals->prev;
            item->prev = tail;
            item->next = removals;
            tail->next = item;
            removals->prev = item;
            AddRef(occupant);
        }
    }
    while (removals->next != removals) {
        void* const occupant = removals->next->value;
        Virtual<uint8_t>(self, 0xC4, occupant);
        ListNode* const item = removals->next;
        ListNode* const next = item->next;
        ListNode* const prev = item->prev;
        prev->next = next;
        next->prev = prev;
        Free(item);
        Release(occupant);
    }
    ClearList(removals);
    Free(removals);
    return 1;
}

// ---------------------------------------------------------------- messages

// The cIGZMessageTarget2 part's slot 3 (0x004B0C10; `target` = object + 8):
// occupants inserted (0x99EF1142, 0x89D3E9F3) and removed (0x99EF1143), the
// view moved (0xA6B79612, 0xA6B79602), pause (0xCAD61E00, 0xEAD61E16), the
// traffic map changed (0x69247DC7), rebuild (0x26D31EC1), 0x86AD10EE.
uint8_t __fastcall DoMessage(void* target, void* /*edx*/, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(target) - 8;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x69247DC7u:
        At<uint8_t>(self, field::kRectDirty) = 1;
        return 1;
    case 0x26D31EC1u:
        Virtual<uint8_t>(self, 0xC8);
        return 1;
    case 0x86AD10EEu: {
        const uint32_t a = Virtual<uint32_t>(message, 0x38);
        const uint32_t b = Virtual<uint32_t>(message, 0x4C);
        Virtual<uint8_t>(self, 0xCC, a, b);
        return 1;
    }
    case 0x89D3E9F3u: {
        void* const occupant = Virtual<void*>(message, 0x80);
        const bool inserted = Virtual<uint32_t>(message, 0x24) != 0;
        if (!occupant || !inserted)
            return 1;
        void* const filter = At<void*>(self, field::kFilterA);
        if (Virtual<uint8_t>(filter, 0xC, Virtual<void*>(occupant, 0xC0)))
            Virtual<uint8_t>(self, 0xC0, occupant, 1u);
        return 1;
    }
    case 0x99EF1142u:
    case 0x99EF1143u: {
        void* const subject = Virtual<void*>(message, 0x28);
        if (Virtual<uint8_t>(At<void*>(self, field::kFilterA), 0xC, subject)) {
            void* network = nullptr;  // the game reuses the message's argument slot
            if (!Virtual<uint8_t>(subject, 0x0, 0xA821EF94u, &network))
                return 1;
            if (type == 0x99EF1142u) {
                void* tunnel = nullptr;
                if (Virtual<uint8_t>(network, 0x58, 7u)) {
                    Virtual<uint8_t>(self, 0xC0, network, 1u);
                } else if (Virtual<uint8_t>(network, 0x0, 0x8A4BD52Bu, &tunnel)) {
                    Release(tunnel);
                    Virtual<uint8_t>(self, 0xC0, network, 1u);
                } else {
                    Virtual<uint8_t>(self, 0xBC, network);
                }
            } else {
                Virtual<uint8_t>(self, 0xC4, network);
            }
            Release(network);
            return 1;
        }
        if (type == 0x99EF1142u) {
            // Filter B: a stop or crossing next to a signal: push the signal's
            // states to it again.
            if (!Virtual<uint8_t>(At<void*>(self, field::kFilterB), 0xC, subject))
                return 1;
            const uint32_t fill = Global<uint32_t>(addr::SC4_ZeroVector3Value);
            uint32_t point[3] = {fill, fill, fill};
            Virtual<void>(subject, 0x20, point);
            // Out slots of the city's position-to-cell: the game's x is an
            // unwritten local, its z the message argument's slot.
            int32_t x = 0;
            int32_t z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(message));
            Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &x, &z);
            void* const occupant = Virtual<void*>(self, 0x2C, x, z);
            if (!occupant)
                return 1;
            const uint32_t key = Key(occupant);
            Iterator found;
            reinterpret_cast<FindFn>(addr::SC4Uint32HashTable_Find)(&Table(self, field::kSignals), &found, &key);
            if (found.node)
                ApplySignals(occupant, static_cast<SignalNode*>(found.node), 1);
            return 1;
        }
        // Removed: an automaton of the occupant leaves every signal's waiting list.
        if (Virtual<uint32_t>(subject, 0x1C) != 0xC7CC75F1u || !Virtual<uint8_t>(subject, 0x58, 0x4005u))
            return 1;
        Hashtable& signals = Table(self, field::kSignals);
        for (auto* node = First<SignalNode>(signals); node; node = Next(signals, node)) {
            ListNode* const sentinel = node->waiting;
            for (ListNode* item = sentinel->next; item != sentinel;) {
                void* const automaton = item->value;
                if (Virtual<void*>(automaton, 0x14) != subject) {
                    item = item->next;
                    continue;
                }
                Release(automaton);
                ListNode* const next = item->next;
                ListNode* const prev = item->prev;
                prev->next = next;
                next->prev = prev;
                Free(item);
                if (node->waiting->next == node->waiting)
                    node->changed = 1;
                item = next;
            }
        }
        return 1;
    }
    case 0xA6B79602u:
        Virtual<void>(self, 0xB8);
        return 1;
    case 0xA6B79612u: {
        if (At<uint8_t>(self, field::kPaused))
            return 1;
        char* const view = Global<char*>(addr::SC4_CameraView);
        auto* const point = reinterpret_cast<float*>(view + 0x3C);
        auto* const kept = &At<float>(self, field::kViewPoint);
        const double moved = reinterpret_cast<DistanceFn>(addr::SC4_DistanceSquared3)(kept, point);
        const float threshold =
            (&Global<float>(addr::cSC4TrafficNetworkMap_ViewMoveThresholds))[At<int32_t>(view, 0xC)];
        if (!(moved > static_cast<double>(threshold)))
            return 1;
        At<uint32_t>(self, field::kViewPoint) = At<uint32_t>(point, 0);
        At<uint32_t>(self, field::kViewPoint + 4) = At<uint32_t>(point, 4);
        At<uint32_t>(self, field::kViewPoint + 8) = At<uint32_t>(point, 8);
        Virtual<void>(self, 0xB8);
        return 1;
    }
    case 0xCAD61E00u:
        At<uint8_t>(self, field::kPaused) = 1;
        return 1;
    case 0xEAD61E16u:
        At<uint8_t>(self, field::kPaused) = 0;
        Virtual<void>(self, 0xB8);
        return 1;
    default:
        return 1;
    }
}

// ---------------------------------------------------------------- registration

constexpr int kMainSlots = 52;
constexpr int kTargetSlots = 4;

const Slot kInitSlots[] = {
    OPENSC4_TM_SLOT(3, 0x004AE4E0, Init),
    OPENSC4_TM_SLOT(4, 0x004B4000, Shutdown),
};

const Slot kOccupantSlots[] = {
    OPENSC4_TM_SLOT(47, 0x004B3380, AddCellObject),
    OPENSC4_TM_SLOT(48, 0x004B4710, AddOccupant),
    OPENSC4_TM_SLOT(49, 0x004B4120, RemoveOccupant),
    OPENSC4_TM_SLOT(50, 0x004B48B0, Rebuild),
};

const Slot kMessageSlots[] = {
    OPENSC4_TM_SLOT(3, 0x004B0C10, DoMessage),
};

bool InstallInit(registry::Mode)
{
    return game::IsBuild641() && PatchSlots("simulators.trafficmap.init", addr::cSC4TrafficNetworkMap_vftable,
                                            kMainSlots, kInitSlots, std::size(kInitSlots));
}

bool InstallOccupants(registry::Mode)
{
    return game::IsBuild641() && PatchSlots("simulators.trafficmap.occupants", addr::cSC4TrafficNetworkMap_vftable,
                                            kMainSlots, kOccupantSlots, std::size(kOccupantSlots));
}

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() && PatchSlots("simulators.trafficmap.messages",
                                            addr::cSC4TrafficNetworkMap_vftable_0x8, kTargetSlots, kMessageSlots,
                                            std::size(kMessageSlots));
}

const bool g_registeredInit = registry::Register({
    "simulators.trafficmap.init",
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable,
    "Our cSC4TrafficNetworkMap Init and Shutdown with the grid and table teardown (docs/traffic-map.md)",
    false,
    InstallInit,
});

const bool g_registeredOccupants = registry::Register({
    "simulators.trafficmap.occupants",
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable,
    "Our cSC4TrafficNetworkMap occupant slots 47-50: cell records, cell objects, signals and lanes of occupants "
    "(docs/traffic-map.md)",
    false,
    InstallOccupants,
});

const bool g_registeredMessages = registry::Register({
    "simulators.trafficmap.messages",
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable_0x8,
    "Our cSC4TrafficNetworkMap DoMessage (docs/traffic-map.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::trafficmap
