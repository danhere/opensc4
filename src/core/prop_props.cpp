// Our cSC4PropDeveloper's props part (build 641; class ID 0x89C48F47): the
// class's code from 0x006F3620 to its end at 0x006F5570. The object part
// (src/core/prop_object.cpp) has everything below 0x006F3620; what this
// file needs from there it calls at the game's addresses.
//
//   0x006F3620  Read (cIGZSerializable slot 4, part +0x8)
//   0x006F3B70  OnPropAdded (name ours): a new prop sorted into the lists
//   0x006F4080  UpdateOptionalProps (name ours): one 8 x 8 cell block
//   0x006F4DB0  UpdateAllOptionalProps (primary slot 7): every block
//   0x006F4E10  StepOptionalProps (name ours): one block per message
//   0x006F4E90  DoMessage (cIGZMessageTarget2 slot 3, part +0x4)
//
// None of them is called from outside the class: the exe has no other call
// or pointer to them than the three vtable slots and DoMessage's and the
// step's own calls. The groups are slot swaps, plus one detour on
// UpdateOptionalProps so that the game's own step and UpdateAll reach ours
// when only simulators.prop.props is on (both callers are plain thiscall
// calls that keep nothing in ECX, EDX or EAX across it: 0x006F4DF6 reloads
// its `this` from the stack, 0x006F4E77 returns right after).
//
// Optional props (UpdateOptionalProps). Lots and bare cells have a
// configuration, a vector of 0x34-byte objects (+0 uint8 kind, +1 uint8
// flags, +0x24 uint32 object ID, +0x28 a vector of candidate exemplar
// IDs). An object of kind 1 with flag bit 0 is an optional prop slot.
// Requesters (+0xA04, added through AddPropRequester) compete for the
// slots. For the block (bx, bz), the cells (8bx..8bx+7, 8bz..8bz+7):
//   1. every requester is asked how much it wants in the rectangle (slot
//      3, +0xC); the answers are weights W[i], their sum S;
//   2. every cell: its lot (lot manager +0xA10 slot 6 with flag 1), which
//      must pass its slots 37 (+0x94) and 46 (+0xB8, 0), counts as one lot
//      and adds a float of its slot 61 (+0xF4) to a sum, and gives the
//      configuration (slot 9, then +0x5C); a cell without a lot gives the
//      one of object +0xA1C (slot 5 at (x, z), then slot 5). The
//      occupants there (occupant manager +0xA14, slot 24 by lot or slot
//      31 by cell) are matched to the configuration by object ID (slot
//      27, +0x6C): an occupant in an optional slot that carries a
//      requester index (property 0x89A1C16C, written by 0x006F1620) is
//      kept in that requester's list, unless the requester is exact (slot
//      5, +0x14) and no longer answers the prop's type for the cell (slot
//      6, +0x18): then the prop is removed and its slot is free again.
//      Optional slots nobody stands in go to the free list;
//   3. no lot in the block: nothing more. Otherwise the target total is
//      floor(sum / lots x slots), slots being kept props plus free slots,
//      shared out as W[i] x (total / S) each, the remainder one by one
//      round robin from requester 1 on (wrapping), skipping requesters
//      whose weight is used up;
//   4. a requester with more props than its target loses ONE, drawn at
//      random among its props (one draw); the slot joins the free list;
//   5. a requester under its target gets props in free slots drawn at
//      random (one draw per try, the slot leaves the free list whether
//      0x006F1620 could make a prop there or not), until it has enough or
//      no free slot is left.
// The draws come from the developer's own Mersenne Twister at +0x38
// (0x00913A14: low + draw x (high - low) >> 32), seeded in Create from the
// time of day (docs/seeds.md); 0x006F1620 draws from it too.
//
// Timing. Message 0x66956814 (the simulator's idle tick) and 0x6695681A
// run StepOptionalProps: state +0x14 goes 1 -> 2 on the first call, then
// each call takes the next block of the iterator +0x18 (which restarts
// when it has been through the city) and updates that one block.
// UpdateAllOptionalProps does every block at once.
//
// New props (OnPropAdded; message 0x99EF1142 with data 3 0xA823821E).
// Nothing for props with property 0x029244DB. By the prop exemplar's
// properties (flags are the prop's slot 21, +0x54, (flag, on)):
//   0x49C9C93C = 1   kept in the vector +0xA20 (the batch that message
//                    0xC9DA96E8/E9 shuffles and 0x86AD10EE works through);
//                    if its state (slot 12, +0x30) differs from +0xA3C it
//                    is switched (slot 11, +0x2C): on only where object
//                    +0xA18 slot 37 says so for its cell, off always
//   0x4A751AD5       uint8, 100 when missing: the prop's slot 17 (+0x44)
//   0x6A3FCC37       a value: the prop joins that value's list in the
//                    hash table +0xA48 and flag 4 is set from service
//                    0x00B43DC8 (interface 0xFA47B3F9 slot 46); message
//                    0xAA375FE8 (value, on) sets flag 4 of a whole list
//   0x4A149631       two floats, start and end hour: list +0xA60, the
//                    prop's slot 13 (+0x34) given both; message 0x44778231
//                    sets flag 1 by the clock (hour + minute / 60), start
//                    after end meaning over midnight, and only when the
//                    time moved by at least +0xA40 since +0xA44
//   0x0A751675       a date, with 0xCA7515CC (two uint8) and 0x4A764564
//                    (1 when missing): the prop's slot 15 (+0x3C); list
//                    +0xA5C, whose props get the simulator's date (slot 7)
//                    through slot 16 (+0x40) on every 0x66956814
//   0x6A959E71       true: flag 8 from object +0xA18 at the prop's cell;
//                    list +0xA64
// A prop with none of the last four is left alone; one with some of them
// has the flags of the missing ones set on (4, 1, 2, 8). Props of type
// 0xAB72FBB3 or 0xCB79919B (slot 7, +0x1C) also go in the list +0xA6C,
// without a reference.
//
// The other messages are passed to the object part: 0x26D31EC1 and
// 0x44778234 (the state +0xA3C, 0x006F10E0), 0x278E6003 and 0x678E6E3A
// (0x006F1270), 0x86AD10EE (0x006F0EC0), 0x99EF1143 (a prop removed,
// 0x006F2FF0, or 0xC772BF98, 0x006F1360), 0x8A07AE00 (a rectangle,
// 0x006F2480), 0xCA5E5027 (a lot, 0x006F2790) and 0xE8BDA2E7 (a lot's
// change: 0x006F2330, then 0xCA5E5027 sent for the lot's cell).
//
// The save record, in Read's order (version uint16, 4 or 5; Write is the
// object part's 0x006F1930):
//   int8 +0x14 (step state), the cell iterator +0x18, uint8 +0xA00,
//   uint32 n and n references (0x89C498BE) for the requesters +0xA04,
//   references +0xA10 (0x681905BD), +0xA14 (0x28326F3E), +0xA1C
//   (0x89A5BA3F), uint32 +0xA2C, +0xA30, +0xA34, +0xA38, bool +0xA3C,
//   uint32 n and n references (0xE9793A65) for the vector +0xA20,
//   uint32 n, then n times {uint32 value, uint32 m, m references} for
//   the hash table +0xA48, three lists {uint32 n, n references}: +0xA60,
//   +0xA5C, +0xA64, and from version 5 bool +0xA68 and the list +0xA6C.
//
// Fields used (offsets from the object's start; names ours):
//   0x04   the cIGZMessageTarget2 part; 0x08 the cIGZSerializable part
//   0x14   int8: the step's state (1 fresh, 2 running; prop_sim.h's
//          kIteratorSet)
//   0x18   the block iterator (0x20 bytes: +0 state, +4 x, +8 z, +0x10
//          x1, +0x14 z1, +0x18 x2, +0x1C z2)
//   0x38   cRZRandomSuper (0x9C8 bytes)
//   0xA00  uint8, saved; not used here
//   0xA04  vector of requesters (end +0xA08): slot 3 (+0xC) the weight
//          for a rectangle, 4 (+0x10) an ID, 5 (+0x14) exact, 6 (+0x18)
//          (vector<uint32>*, x, z) the type wanted at a cell
//   0xA10  the lot manager; 0xA14 the service 0x00B43D10 (slot 29, +0x74,
//          removes an occupant); 0xA1C the city's slot 42 object, the
//          source of bare (network) cells' configurations
//   0xA18  the service 0x00B43DA0, whose slot 37 (+0x94) answers for a
//          cell (position / 16) whether its props are on; not saved
//   0xA20  vector of props with property 0x49C9C93C (end +0xA24)
//   0xA2C  uint32: the batch done so far; 0xA30 uint32: 0 idle, 1 or 2
//          the change running; 0xA34 uint32: batches (props / 8, rounded
//          up); 0xA38 uint32, saved
//   0xA3C  uint8: the state the 0x49C9C93C props follow (service
//          0x00B43DDC slot 9; by its use, night)
//   0xA40  float: the least clock change worth a pass; 0xA44 float: the
//          clock at the last pass
//   0xA48  hash table of {uint32 value, list of props} (count +0xA58)
//   0xA5C, 0xA60, 0xA64, 0xA6C  lists of props (a pointer to the
//          sentinel each); 0xA68 uint8, saved
#include "core/prop_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::prop_sim {
namespace {

namespace local {
constexpr std::size_t kStepState = 0x14;
constexpr std::size_t kIterator = 0x18;
constexpr std::size_t kRandom = 0x38;
constexpr std::size_t kByteA00 = 0xA00;
constexpr std::size_t kRequesters = 0xA04; // begin; end +0xA08
constexpr std::size_t kRequestersEnd = 0xA08;
constexpr std::size_t kLots = 0xA10;
constexpr std::size_t kOccupants = 0xA14;
constexpr std::size_t kCellTest = 0xA18;
constexpr std::size_t kCellConfigs = 0xA1C;
constexpr std::size_t kStateProps = 0xA20; // begin; end +0xA24
constexpr std::size_t kStatePropsEnd = 0xA24;
constexpr std::size_t kBatchDone = 0xA2C;
constexpr std::size_t kChange = 0xA30;
constexpr std::size_t kBatches = 0xA34;
constexpr std::size_t kUint32A38 = 0xA38;
constexpr std::size_t kState = 0xA3C;
constexpr std::size_t kClockStep = 0xA40;
constexpr std::size_t kClockLast = 0xA44;
constexpr std::size_t kValueTable = 0xA48; // element count +0x10
constexpr std::size_t kDatedProps = 0xA5C;
constexpr std::size_t kTimedProps = 0xA60;
constexpr std::size_t kCellProps = 0xA64;
constexpr std::size_t kByteA68 = 0xA68;
constexpr std::size_t kTypeProps = 0xA6C;
} // namespace local

constexpr std::size_t kMessagePart = 0x4;
constexpr std::size_t kSerializablePart = 0x8;
constexpr uint32_t kIidProp = 0xE9793A65;

// A node of the game's lists of pointers (0x0C bytes from the STL
// allocator); a list is one pointer to its sentinel.
struct PtrNode {
    PtrNode* next;
    PtrNode* prev;
    void* value;
};

// An optional prop slot: the cell (uint8 x, uint8 z; the game leaves the
// upper two bytes as they were on its stack), the configuration vector,
// the object's index in it and the occupant standing there (null: free).
struct Slot {
    uint32_t cell;
    void* config;
    int32_t index;
    void* occupant;
};

struct SlotNode {
    SlotNode* next;
    SlotNode* prev;
    Slot value;
};
static_assert(sizeof(SlotNode) == 0x18);

struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

struct ListVector {
    SlotNode** begin;
    SlotNode** end;
    SlotNode** capacity;
};

// STLport's vector<bool>: two bit iterators and the end of storage.
struct BitVector {
    uint32_t* start;
    int32_t startOffset;
    uint32_t* finish;
    int32_t finishOffset;
    uint32_t* endOfStorage;
};

// A {value, list of props} pair of the hash table +0xA48 and what its
// insert returns (9 bytes written).
struct ValuePair {
    uint32_t key;
    PtrNode* list;
};

struct InsertResult {
    PtrNode* node; // a table node: {next, key, list}
    void* table;
    uint8_t inserted;
};

struct FindResult {
    char* node;
    void* table;
};

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* memory);
using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
using ReadPartFn = uint8_t(__cdecl*)(void* stream, void* target);
using GetUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using PushBackFn = void(__thiscall*)(void* container, void* const* value);
using SelfFn = void(__thiscall*)(void* self);
using SelfArgFn = void(__thiscall*)(char* self, void* argument);
using SelfUintFn = void(__thiscall*)(char* self, uint32_t argument);
using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
using BetweenFn = uint32_t(__thiscall*)(void* random, uint32_t low, uint32_t high);
using PlaceFn = uint8_t(__thiscall*)(char* self, Slot* slot, int32_t requester);
using FindFn = void(__thiscall*)(void* table, FindResult* out, const uint32_t* key);

void* Allocate(std::size_t size) { return reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size); }
void Free(void* memory) { reinterpret_cast<FreeFn>(addr::SC4_Free)(memory); }

// __ftol (0x009EEF04): truncation; the low word of its int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// The developer's own push_back of a list of pointers, inline in the game.
void PushBack(PtrNode* sentinel, void* value)
{
    auto* const node = static_cast<PtrNode*>(Allocate(sizeof(PtrNode)));
    node->value = value;
    PtrNode* const last = sentinel->prev;
    node->next = sentinel;
    node->prev = last;
    last->next = node;
    sentinel->prev = node;
}

// The same through the game's helper (0x00427640), which takes the list.
void PushBackList(char* self, std::size_t list, void* value)
{
    reinterpret_cast<PushBackFn>(addr::SC4StlList_PushBack)(self + list, &value);
}

PtrNode* NewList()
{
    auto* const sentinel = static_cast<PtrNode*>(Allocate(sizeof(PtrNode)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

// Frees a list's nodes, then its sentinel (no references released).
template <typename Node>
void DestroyList(Node* sentinel)
{
    for (Node* node = sentinel->next; node != sentinel;) {
        Node* const freed = node;
        node = node->next;
        Free(freed);
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    Free(sentinel);
}

// --- Read (0x006F3620) ------------------------------------------------------------

bool ReadUint32(void* stream, uint32_t* value) { return Virtual<uint8_t>(stream, 0x24, value) != 0; }

void ReadReference(void* stream, uint32_t iid, void** object)
{
    reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, iid, object, 0u);
}

// One of the record's plain lists: a count and that many prop references,
// the non-null ones appended. `prop` is the game's one stack slot for all
// of these lists: it is cleared before the first list only, so a null
// reference repeats the reference read before it, if ReadReference leaves
// its target alone then (copied as it is).
void ReadPropList(void* stream, char* self, std::size_t list, void*& prop)
{
    uint32_t count = 0;
    ReadUint32(stream, &count);
    for (uint32_t i = 0; i < count; ++i) {
        ReadReference(stream, kIidProp, &prop);
        if (prop)
            PushBack(At<PtrNode*>(self, list), prop);
    }
}

uint8_t __fastcall Read(char* part, void*, void* stream)
{
    using FillFn = void(__thiscall*)(void* vector, uint32_t count, const void* value);
    using FillInsertFn = void(__thiscall*)(void* vector, void* where, uint32_t count, const void* value);
    using ResizeFn = void(__thiscall*)(void* table, uint32_t count);
    using InsertFn = void(__thiscall*)(void* table, InsertResult* out, const ValuePair* pair);
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;

    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version < 4)
        return 0;
    Virtual<uint8_t>(stream, 0x10, self + local::kStepState);
    reinterpret_cast<ReadPartFn>(addr::SC4Stream_ReadCellIterator)(stream, self + local::kIterator);
    Virtual<uint8_t>(stream, 0x14, self + local::kByteA00);

    uint32_t count = 0;
    ReadUint32(stream, &count);
    void* null = nullptr;
    reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(self + local::kRequesters, count, &null);
    for (uint32_t i = 0; i < count; ++i)
        ReadReference(stream, 0x89C498BE, At<void**>(self, local::kRequesters) + i);
    ReadReference(stream, 0x681905BD, &At<void*>(self, local::kLots));
    ReadReference(stream, 0x28326F3E, &At<void*>(self, local::kOccupants));
    ReadReference(stream, 0x89A5BA3F, &At<void*>(self, local::kCellConfigs));
    ReadUint32(stream, &At<uint32_t>(self, local::kBatchDone));
    ReadUint32(stream, &At<uint32_t>(self, local::kChange));
    ReadUint32(stream, &At<uint32_t>(self, local::kBatches));
    ReadUint32(stream, &At<uint32_t>(self, local::kUint32A38));
    reinterpret_cast<ReadPartFn>(addr::SC4Stream_ReadBool)(stream, self + local::kState);

    // The state props: the vector resized to the count (shrinking only
    // moves its end), then filled.
    ReadUint32(stream, &count);
    {
        void**& begin = At<void**>(self, local::kStateProps);
        void**& end = At<void**>(self, local::kStatePropsEnd);
        null = nullptr;
        const uint32_t size = static_cast<uint32_t>(end - begin);
        if (count < size)
            end = begin + count;
        else
            reinterpret_cast<FillInsertFn>(addr::SC4PtrVector_FillInsert)(self + local::kStateProps, end,
                                                                         count - size, &null);
        for (uint32_t i = 0; i < count; ++i)
            ReadReference(stream, kIidProp, At<void**>(self, local::kStateProps) + i);
    }

    // The value table: per value an empty list inserted (the game builds
    // an empty list, a copy of it for the pair, and frees both after the
    // insert copied it again), then the props appended to the table's
    // list. A value already there has its props read and dropped.
    uint32_t values = 0;
    ReadUint32(stream, &values);
    for (uint32_t v = 0; v < values; ++v) {
        uint32_t value = 0;
        ReadUint32(stream, &value);
        PtrNode* const empty = NewList();
        ValuePair pair{value, NewList()};
        for (PtrNode* node = empty->next; node != empty; node = node->next)
            PushBack(pair.list, node->value);
        char* const table = self + local::kValueTable;
        reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(table, At<uint32_t>(table, 0x10) + 1);
        InsertResult result{};
        reinterpret_cast<InsertFn>(addr::SC4KeyListTable_InsertUnique)(table, &result, &pair);
        DestroyList(pair.list);
        DestroyList(empty);
        PtrNode** const list = result.inserted ? reinterpret_cast<PtrNode**>(&result.node->value) : nullptr;

        uint32_t props = 0;
        ReadUint32(stream, &props);
        for (uint32_t i = 0; i < props; ++i) {
            void* prop = nullptr;
            ReadReference(stream, kIidProp, &prop);
            if (list && prop)
                PushBack(*list, prop);
        }
    }

    void* prop = nullptr;
    ReadPropList(stream, self, local::kTimedProps, prop);
    ReadPropList(stream, self, local::kDatedProps, prop);
    ReadPropList(stream, self, local::kCellProps, prop);
    if (version > 4) {
        reinterpret_cast<ReadPartFn>(addr::SC4Stream_ReadBool)(stream, self + local::kByteA68);
        ReadPropList(stream, self, local::kTypeProps, prop);
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// --- OnPropAdded (0x006F3B70) -----------------------------------------------------

// Object +0xA18's answer (slot 37) for the cell of a position (x and z
// over 16, truncated; z converted first).
uint8_t CellTest(void* test, const float* position)
{
    const int32_t z = Ftol(static_cast<double>(position[2]) * 0.0625);
    const int32_t x = Ftol(static_cast<double>(position[0]) * 0.0625);
    return Virtual<uint8_t>(test, 0x94, x, z);
}

void OnPropAdded(char* self, void* occupant)
{
    using ConstructListFn = PtrNode**(__thiscall*)(PtrNode** list, void* allocator);
    using ConstructPairFn = ValuePair*(__thiscall*)(ValuePair* pair, const uint32_t* key, PtrNode* const* list);
    using InsertFn = InsertResult*(__thiscall*)(void* table, InsertResult* out, const ValuePair* pair);
    static bool logged = false;
    FirstCall("OnPropAdded", logged);

    void* const holder = Virtual<void*>(occupant, 0x18);
    if (Virtual<uint8_t>(holder, 0xC, 0x029244DBu))
        return;

    uint8_t follows = 0;
    if (reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8)(holder, 0x49C9C93C, &follows) && follows == 1) {
        void* stateProp = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidProp, &stateProp)) {
            // The vector keeps the reference QueryInterface took.
            reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack)(self + local::kStateProps, &stateProp);
            const uint32_t state = At<uint8_t>(self, local::kState) != 0 ? 1u : 0u;
            if (Virtual<uint32_t>(stateProp, 0x30) != state) {
                if (state == 1) {
                    float position[3];
                    Virtual<void*>(occupant, 0x20, position);
                    if (CellTest(At<void*>(self, local::kCellTest), position))
                        Virtual<uint32_t>(stateProp, 0x2C, 1u);
                } else {
                    Virtual<uint32_t>(stateProp, 0x2C, state);
                }
            }
        }
    }

    void* prop = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop))
        return;
    if (Virtual<uint8_t>(holder, 0xC, 0x6A3FCC37u) || Virtual<uint8_t>(holder, 0xC, 0x4A149631u) ||
        Virtual<uint8_t>(holder, 0xC, 0x0A751675u) || Virtual<uint8_t>(holder, 0xC, 0x6A959E71u)) {
        Virtual<uint32_t>(prop, 0x2C, 1u);
        // The game pushes the whole stack word of these uint8 values; the
        // bytes above them are what its stack held. Zeros here.
        uint8_t chance = 0x64;
        reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8)(holder, 0x4A751AD5, &chance);
        Virtual<uint32_t>(prop, 0x44, static_cast<uint32_t>(chance));

        uint32_t value = 0;
        if (reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32)(holder, 0x6A3FCC37, &value) && value != 0) {
            void* const service = Global<void*>(addr::SC4_Service43DC8);
            void* shown = nullptr;
            if (service && Virtual<uint8_t>(service, 0x0, 0xFA47B3F9u, &shown)) {
                char* const table = self + local::kValueTable;
                FindResult found{};
                reinterpret_cast<FindFn>(addr::SC4Uint32HashTable_Find)(table, &found, &value);
                char* node = found.node;
                if (!node) {
                    PtrNode* empty = nullptr;
                    void* unused = nullptr;
                    reinterpret_cast<ConstructListFn>(addr::SC4RefList_Construct)(&empty, &unused);
                    ValuePair pair{};
                    reinterpret_cast<ConstructPairFn>(addr::cSC4PropDeveloper_KeyListPairConstruct)(&pair, &value, &empty);
                    InsertResult result{};
                    reinterpret_cast<InsertFn>(addr::cSC4PropDeveloper_KeyListTableInsert)(table, &result, &pair);
                    reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(&pair.list);
                    reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(&empty);
                    node = reinterpret_cast<char*>(result.node);
                }
                if (node) {
                    reinterpret_cast<PushBackFn>(addr::SC4StlList_PushBack)(node + 8, &prop);
                    Virtual<uint32_t>(prop, 0x4);
                    const uint8_t on = Virtual<uint8_t>(shown, 0xB8, value);
                    Virtual<uint32_t>(prop, 0x54, 4u, static_cast<uint32_t>(on));
                }
                Virtual<uint32_t>(shown, 0x8);
            }
        } else {
            Virtual<uint32_t>(prop, 0x54, 4u, 1u);
        }

        if (void* const hours = Virtual<void*>(holder, 0x24, 0x4A149631u)) {
            void* const variant = Virtual<void*>(hours, 0x14);
            const float* const pair = Virtual<const float*>(variant, 0x1BC);
            // Equal hours (not NaN) are no interval.
            if (Virtual<uint32_t>(variant, 0x10) >= 2 && !(pair[0] == pair[1])) {
                PushBackList(self, local::kTimedProps, prop);
                Virtual<uint32_t>(prop, 0x4);
                uint32_t bits[2];
                std::memcpy(bits, pair, sizeof(bits));
                Virtual<uint32_t>(prop, 0x34, bits[0], bits[1]);
            }
        } else {
            Virtual<uint32_t>(prop, 0x54, 1u, 1u);
        }

        if (void* const date = Virtual<void*>(holder, 0x24, 0x0A751675u)) {
            void* const variant = Virtual<void*>(date, 0x14);
            const uint32_t start = *Virtual<const uint32_t*>(variant, 0x124);
            uint8_t first = 0;
            uint8_t second = 0;
            uint32_t interval = 1;
            if (void* const extra = Virtual<void*>(holder, 0x24, 0xCA7515CCu)) {
                void* const bytes = Virtual<void*>(extra, 0x14);
                const uint8_t* const pair = Virtual<const uint8_t*>(bytes, 0x17C);
                if (Virtual<uint32_t>(bytes, 0x10) >= 2) {
                    first = pair[0];
                    second = pair[1];
                }
            }
            reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32)(holder, 0x4A764564, &interval);
            Virtual<uint32_t>(prop, 0x3C, start, interval, static_cast<uint32_t>(first),
                              static_cast<uint32_t>(second));
            PushBackList(self, local::kDatedProps, prop);
            Virtual<uint32_t>(prop, 0x4);
        } else {
            Virtual<uint32_t>(prop, 0x54, 2u, 1u);
        }

        uint8_t byCell = 0;
        reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyBool)(holder, 0x6A959E71, &byCell);
        if (byCell) {
            const float zero = Global<float>(addr::cSC4PropDeveloper_ZeroB4A5B8);
            float position[3] = {zero, zero, zero};
            void* const propOccupant = Virtual<void*>(prop, 0xC);
            Virtual<void*>(propOccupant, 0x20, position);
            void* const test = At<void*>(self, local::kCellTest);
            const uint32_t on = test && CellTest(test, position) ? 1u : 0u;
            Virtual<uint32_t>(prop, 0x54, 8u, on);
            PushBackList(self, local::kCellProps, prop);
            Virtual<uint32_t>(prop, 0x4);
        } else {
            Virtual<uint32_t>(prop, 0x54, 8u, 1u);
        }
    }

    const uint32_t type = Virtual<uint32_t>(prop, 0x1C);
    if (type == 0xAB72FBB3 || type == 0xCB79919B)
        PushBack(At<PtrNode*>(self, local::kTypeProps), prop); // no reference taken
    Virtual<uint32_t>(prop, 0x8);
}

// --- UpdateOptionalProps (0x006F4080) ---------------------------------------------

// The word and mask of a bit, as the game's inline bit iterator finds them.
uint32_t* BitWord(const BitVector& bits, int32_t index, uint32_t& mask)
{
    const int32_t at = bits.startOffset + index;
    uint32_t* word = bits.start + at / 32;
    int32_t bit = at % 32;
    if (bit < 0) {
        bit += 32;
        --word;
    }
    mask = 1u << bit;
    return word;
}

void SetBit(const BitVector& bits, int32_t index)
{
    uint32_t mask;
    uint32_t* const word = BitWord(bits, index, mask);
    *word |= mask;
}

void ClearBit(const BitVector& bits, int32_t index)
{
    uint32_t mask;
    uint32_t* const word = BitWord(bits, index, mask);
    *word &= ~mask;
}

bool TestBit(const BitVector& bits, int32_t index)
{
    uint32_t mask;
    return (*BitWord(bits, index, mask) & mask) != 0;
}

void ResizeBits(BitVector* bits, int32_t count)
{
    using ResizeFn = void(__thiscall*)(BitVector* bits, uint32_t count, uint32_t value);
    reinterpret_cast<ResizeFn>(addr::SC4BitVector_Resize60D310)(bits, static_cast<uint32_t>(count), 0u);
}

SlotNode* NewSlotList()
{
    auto* const sentinel = static_cast<SlotNode*>(Allocate(sizeof(SlotNode)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

void PushSlot(SlotNode* sentinel, const Slot& slot)
{
    auto* const node = static_cast<SlotNode*>(Allocate(sizeof(SlotNode)));
    node->value = slot;
    SlotNode* const last = sentinel->prev;
    node->next = sentinel;
    node->prev = last;
    last->next = node;
    sentinel->prev = node;
}

void EraseSlot(SlotNode* node)
{
    SlotNode* const next = node->next;
    SlotNode* const prev = node->prev;
    prev->next = next;
    next->prev = prev;
    Free(node);
}

// The node `steps` after the list's first.
SlotNode* NthSlot(SlotNode* sentinel, uint32_t steps)
{
    SlotNode* node = sentinel->next;
    for (; steps; --steps)
        node = node->next;
    return node;
}

uint32_t CellWord(int32_t x, int32_t z)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(x)) | static_cast<uint32_t>(static_cast<uint8_t>(z)) << 8;
}

void UpdateOptionalProps(char* self, int32_t blockX, int32_t blockZ)
{
    using AssignListsFn = void(__thiscall*)(ListVector* lists, uint32_t count, SlotNode* const* value);
    using AssignFn = void(__thiscall*)(Uint32Vector* vector, uint32_t count, const uint32_t* value);
    using InsertNFn = void(__thiscall*)(Uint32Vector* vector, uint32_t* where, const uint32_t* value,
                                        void* allocator, uint32_t count, uint32_t atEnd);
    using ReserveBitsFn = void(__thiscall*)(BitVector* bits, uint32_t count);
    using DestroyListsFn = void(__cdecl*)(SlotNode** first, SlotNode** last, void* dummy);
    static bool logged = false;
    FirstCall("UpdateOptionalProps", logged);

    const int32_t count =
        static_cast<int32_t>(At<char*>(self, local::kRequestersEnd) - At<char*>(self, local::kRequesters)) >> 2;

    // One list of kept props per requester, and the free slots.
    ListVector kept{};
    SlotNode* const prototype = NewSlotList();
    reinterpret_cast<AssignListsFn>(addr::cSC4PropDeveloper_ItemListVectorAssign)(&kept, static_cast<uint32_t>(count),
                                                                         &prototype);
    SlotNode* const freeSlots = NewSlotList();
    const int32_t rect[4] = {blockX << 3, blockZ << 3, (blockX << 3) + 7, (blockZ << 3) + 7};

    // What every requester wants in the rectangle.
    Uint32Vector wanted{};
    BitVector wants{};
    uint32_t wantedTotal = 0;
    uint32_t zero = 0;
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(&wanted, static_cast<uint32_t>(count), &zero);
    ResizeBits(&wants, count);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t weight = Virtual<uint32_t>(At<void**>(self, local::kRequesters)[i], 0xC, rect);
        wanted.begin[i] = weight;
        if (weight > 0)
            SetBit(wants, i);
        else
            ClearBit(wants, i);
        wantedTotal += wanted.begin[i];
    }

    float valueSum = 0.0f;
    int32_t lots = 0;
    int32_t keptTotal = 0;
    int32_t slotTotal = 0;
    Uint32Vector have{};
    zero = 0;
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(&have, static_cast<uint32_t>(count), &zero);
    // A vector of one type ID for the exact requesters' question.
    Uint32Vector types{};
    uint8_t allocator = 0;
    zero = 0;
    reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN)(&types, nullptr, &zero, &allocator, 1u, 1u);

    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            void* const lot = Virtual<void*>(At<void*>(self, local::kLots), 0x18, x, z, 1u);
            void* config = nullptr;
            if (lot) {
                if (!Virtual<uint8_t>(lot, 0x94))
                    continue;
                if (!Virtual<uint32_t>(lot, 0xB8, 0u))
                    continue;
                ++lots;
                float buffer[2] = {0.0f, 0.0f};
                const float* const value = Virtual<const float*>(lot, 0xF4, buffer);
                valueSum = static_cast<float>(static_cast<double>(valueSum) + static_cast<double>(*value));
                void* const source = Virtual<void*>(lot, 0x24);
                if (!source)
                    continue;
                config = Virtual<void*>(source, 0x5C);
            } else {
                void* const source = Virtual<void*>(At<void*>(self, local::kCellConfigs), 0x14, x, z);
                if (!source)
                    continue;
                config = Virtual<void*>(source, 0x14);
            }
            if (!config)
                continue;

            // The cell's optional slots: a bit per configuration object.
            const int32_t objects =
                static_cast<int32_t>(At<char*>(config, 0x4) - At<char*>(config, 0x0)) / 0x34;
            int32_t freeHere = 0;
            int32_t keptHere = 0;
            BitVector optional{};
            reinterpret_cast<ReserveBitsFn>(addr::cSC4PropDeveloper_BitVectorReserve)(&optional,
                                                                               static_cast<uint32_t>(objects));
            ResizeBits(&optional, objects);
            for (int32_t i = 0; i < objects; ++i) {
                const char* const object = At<char*>(config, 0x0) + i * 0x34;
                if (object[0] == 1 && (object[1] & 1)) {
                    SetBit(optional, i);
                    ++freeHere;
                }
            }

            // The occupants there (a list of the allocator at 0x00B207E4).
            void* const nodes = ListNodeAllocator();
            auto* const occupants = Virtual<PtrNode*>(nodes, 0xC, 0xCu);
            occupants->next = occupants;
            occupants->prev = occupants;
            PtrNode* list = occupants;
            void* const manager = At<void*>(self, local::kOccupants);
            if (lot)
                Virtual<uint32_t>(manager, 0x60, &list, lot);
            else
                Virtual<uint32_t>(manager, 0x7C, &list, x, z);
            PtrNode* const sentinel = list;
            for (PtrNode* node = sentinel->next; node != sentinel; node = node->next) {
                void* const occupant = node->value;
                uint32_t id = 0;
                if (Virtual<uint8_t>(occupant, 0x6C, &id)) {
                    int32_t index = 0;
                    for (; index < objects; ++index) {
                        if (At<uint32_t>(At<char*>(config, 0x0) + index * 0x34, 0x24) == id)
                            break;
                    }
                    if (index >= objects) {
                        // An occupant the configuration does not know is
                        // counted off the kept props.
                        --keptHere;
                        Virtual<uint32_t>(occupant, 0x8);
                        continue;
                    }
                    if (TestBit(optional, index)) {
                        ClearBit(optional, index);
                        --freeHere;
                        uint32_t requester = 0;
                        if (reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32)(
                                Virtual<void*>(occupant, 0x18), 0x89A1C16C, &requester)) {
                            bool keep = true;
                            if (Virtual<uint8_t>(At<void**>(self, local::kRequesters)[requester], 0x14)) {
                                keep = false;
                                void* prop = nullptr;
                                if (Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop)) {
                                    types.begin[0] = Virtual<uint32_t>(prop, 0x1C);
                                    const uint32_t answer = Virtual<uint32_t>(
                                        At<void**>(self, local::kRequesters)[requester], 0x18, &types, x, z);
                                    keep = answer == types.begin[0];
                                    Virtual<uint32_t>(prop, 0x8);
                                }
                            }
                            if (keep) {
                                Virtual<uint32_t>(occupant, 0x4);
                                PushSlot(kept.begin[requester], Slot{CellWord(x, z), config, index, occupant});
                                ++have.begin[requester];
                                ++keptHere;
                            } else {
                                Virtual<uint32_t>(At<void*>(self, local::kOccupants), 0x74, occupant);
                                SetBit(optional, index);
                                ++freeHere;
                            }
                        }
                    }
                }
                Virtual<uint32_t>(occupant, 0x8);
            }
            for (int32_t i = 0; i < objects; ++i) {
                if (TestBit(optional, i))
                    PushSlot(freeSlots, Slot{CellWord(x, z), config, i, nullptr});
            }
            keptTotal += keptHere;
            slotTotal += keptHere + freeHere;
            reinterpret_cast<SelfFn>(addr::SC4List_Clear2)(&list);
            PtrNode* const emptied = list;
            Virtual<uint32_t>(ListNodeAllocator(), 0x10, emptied);
            if (optional.start)
                Free(optional.start);
        }
    }

    uint32_t* targets = nullptr;
    if (lots != 0) {
        // The target total: the lots' mean value times the slots, rounded
        // down (the game's float trick: + 1.5 x 2^23 stored as a float,
        // its bits less that constant's, then one more if that is exact).
        const double product =
            static_cast<double>(valueSum) / static_cast<double>(lots) * static_cast<double>(slotTotal);
        const float shifted = static_cast<float>(product - 0.5 + 12582912.0);
        uint32_t bits;
        std::memcpy(&bits, &shifted, sizeof(bits));
        uint32_t total = bits + 0xB4C00000u;
        if (product == static_cast<double>(static_cast<int32_t>(total + 1)))
            total += 1;

        if (count != 0)
            targets = static_cast<uint32_t*>(Allocate(static_cast<std::size_t>(count) * 4));
        if (count > 0)
            std::memset(targets, 0, static_cast<std::size_t>(count) * 4);
        // Shared by weight (no weight at all with lots there divides by
        // zero, in the game too), the remainder round robin from the
        // second requester on.
        const uint32_t each = total / wantedTotal;
        const uint32_t remainder = total % wantedTotal;
        for (int32_t i = 0; i < count; ++i)
            targets[i] = wanted.begin[i] * each;
        const int32_t extra = static_cast<int32_t>(remainder < wantedTotal ? remainder : wantedTotal);
        int32_t at = 0;
        for (int32_t given = 0; given < extra; ++given) {
            uint32_t left;
            do {
                at = at == count - 1 ? 0 : at + 1;
                left = wanted.begin[at];
            } while (left == 0);
            wanted.begin[at] = left - 1;
            ++targets[at];
        }

        // One prop off every requester over its target.
        for (int32_t i = 0; i < count; ++i) {
            if (targets[i] >= have.begin[i])
                continue;
            const uint32_t pick = reinterpret_cast<BetweenFn>(addr::cRZRandomSuper_Between)(
                self + local::kRandom, 0u, have.begin[i]);
            SlotNode* const node = NthSlot(kept.begin[i], pick);
            const Slot slot = node->value;
            Virtual<uint32_t>(At<void*>(self, local::kOccupants), 0x74, slot.occupant);
            Virtual<uint32_t>(slot.occupant, 0x8);
            EraseSlot(node);
            PushSlot(freeSlots, Slot{slot.cell, slot.config, slot.index, nullptr});
            ++slotTotal;
        }

        // Free slots drawn for the requesters under their target.
        int32_t freeCount = slotTotal - keptTotal;
        for (int32_t i = 0; i < count; ++i) {
            if (targets[i] <= have.begin[i])
                continue;
            uint32_t need = targets[i] - have.begin[i];
            while (freeCount > 0) {
                const uint32_t pick = reinterpret_cast<BetweenFn>(addr::cRZRandomSuper_Between)(
                    self + local::kRandom, 0u, static_cast<uint32_t>(freeCount));
                SlotNode* const node = NthSlot(freeSlots, pick);
                if (reinterpret_cast<PlaceFn>(addr::cSC4PropDeveloper_PlaceProp)(self, &node->value, i))
                    --need;
                EraseSlot(node);
                --freeCount;
                if (need == 0)
                    break;
            }
        }
    }

    for (int32_t i = 0; i < count; ++i) {
        SlotNode* const sentinel = kept.begin[i];
        for (SlotNode* node = sentinel->next; node != sentinel; node = node->next)
            Virtual<uint32_t>(node->value.occupant, 0x8);
    }
    if (targets)
        Free(targets);
    if (types.begin)
        Free(types.begin);
    if (have.begin)
        Free(have.begin);
    if (wants.start)
        Free(wants.start);
    if (wanted.begin)
        Free(wanted.begin);
    DestroyList(freeSlots);
    DestroyList(prototype);
    uint8_t dummy = 0;
    SlotNode** const first = kept.begin;
    reinterpret_cast<DestroyListsFn>(addr::SC4StdList_DestroyListRange)(first, kept.end, &dummy);
    if (first)
        Free(first);
}

// --- UpdateAllOptionalProps (0x006F4DB0), StepOptionalProps (0x006F4E10) ----------

void __fastcall UpdateAllOptionalProps(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateAllOptionalProps", logged);
    const int32_t blocksX = static_cast<int32_t>(Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x174) >> 3);
    const int32_t blocksZ = static_cast<int32_t>(Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x178) >> 3);
    for (int32_t x = 0; x < blocksX; ++x) {
        for (int32_t z = 0; z < blocksZ; ++z)
            UpdateOptionalProps(self, x, z);
    }
}

// Whether simulators.prop.props is on: our DoMessage then steps with our
// code, otherwise through the game's 0x006F4E10.
bool g_propsOurs = false;

using UpdateFn = void(__fastcall*)(char* self, void*, int32_t blockX, int32_t blockZ);
UpdateFn g_originalUpdate = nullptr;

void __fastcall UpdateOptionalPropsDetour(char* self, void*, int32_t blockX, int32_t blockZ)
{
    UpdateOptionalProps(self, blockX, blockZ);
}

void StepOptionalProps(char* self, uint32_t argument)
{
    if (!g_propsOurs) {
        reinterpret_cast<SelfUintFn>(addr::cSC4PropDeveloper_StepOptionalProps)(self, argument);
        return;
    }
    static bool logged = false;
    FirstCall("StepOptionalProps", logged);
    const int8_t state = At<int8_t>(self, local::kStepState);
    if (state == 1) {
        At<int8_t>(self, local::kStepState) = 2;
        return;
    }
    if (state != 2)
        return;
    char* const iterator = self + local::kIterator;
    const auto next = reinterpret_cast<NextFn>(addr::SC4CellIterator_Next);
    int32_t x = 0;
    int32_t z = 0;
    if (!next(iterator, &x, &z)) {
        // Through the city: the iterator started again (state 2, done,
        // when its rectangle does not hold (0, 0)).
        At<int32_t>(iterator, 0x4) = 0;
        At<int32_t>(iterator, 0x8) = 0;
        At<uint8_t>(iterator, 0x0) = 0;
        if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
            At<int32_t>(iterator, 0x1C) < 0)
            At<uint8_t>(iterator, 0x0) = 2;
        next(iterator, &x, &z);
    }
    UpdateOptionalProps(self, x, z);
}

// --- DoMessage (0x006F4E90) -------------------------------------------------------

void* Service(std::uintptr_t address) { return Global<void*>(address); }

void CallObjectPart(std::uintptr_t function, char* self, void* argument)
{
    reinterpret_cast<SelfArgFn>(function)(self, argument);
}

void CallObjectPart(std::uintptr_t function, char* self, uint32_t argument)
{
    reinterpret_cast<SelfUintFn>(function)(self, argument);
}

// Message 0x44778231: the props with hours shown or hidden by the clock.
void UpdateTimedProps(char* self)
{
    void* const clock = Service(addr::SC4_Clock43DBC);
    // Unsigned to float as the game does it; minutes / 60 rounded to a
    // float before the hour is added.
    constexpr float kSixtieth = 0.016666668f; // 0x3C888889 at 0x00A94124
    const float minutes =
        static_cast<float>(static_cast<double>(Virtual<uint32_t>(clock, 0x34)) * static_cast<double>(kSixtieth));
    const float time =
        static_cast<float>(static_cast<double>(Virtual<uint32_t>(clock, 0x28)) + static_cast<double>(minutes));
    const double moved = static_cast<double>(At<float>(self, local::kClockLast)) - static_cast<double>(time);
    if (!((moved < 0 ? -moved : moved) >= static_cast<double>(At<float>(self, local::kClockStep))))
        return;
    At<float>(self, local::kClockLast) = time;

    PtrNode*& list = At<PtrNode*>(self, local::kTimedProps);
    for (PtrNode* node = list->next; node != list;) {
        void* const prop = node->value;
        if (prop) {
            float end = 0.0f;
            float start = 0.0f;
            Virtual<uint32_t>(prop, 0x38, &start, &end);
            uint32_t on;
            if (!(end >= start)) // over midnight (or NaN)
                on = time <= end || time >= start ? 1u : 0u;
            else
                on = time >= start && time <= end ? 1u : 0u;
            Virtual<uint32_t>(prop, 0x54, 1u, on);
            node = node->next;
        } else {
            // A null entry takes the last entry's value and the last
            // node is dropped. When the null entry is the last node
            // itself the game goes on from the freed node; so do we.
            PtrNode* const sentinel = list;
            if (node->next != sentinel)
                node->value = sentinel->prev->value;
            reinterpret_cast<SelfFn>(addr::cSC4PropDeveloper_ListPopBack)(&list);
        }
    }
}

uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - kMessagePart;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x44778231:
        UpdateTimedProps(self);
        return 1;
    case 0x26D31EC1: {
        const uint8_t state = Virtual<uint8_t>(Service(addr::SC4_Transform43DDC), 0x24);
        At<uint8_t>(self, local::kState) = state;
        CallObjectPart(addr::cSC4PropDeveloper_SetTimedProps, self, state ? 1u : 0u);
        return 1;
    }
    case 0x44778234: {
        void* const clock = Service(addr::SC4_Clock43DBC);
        if (!clock)
            return 1;
        const uint32_t which = Virtual<uint32_t>(message, 0x38);
        if (which == 0) {
            if (Virtual<uint32_t>(clock, 0x18) == 2)
                CallObjectPart(addr::cSC4PropDeveloper_SetTimedProps, self, 1u);
        } else if (which == 1) {
            if (Virtual<uint32_t>(clock, 0x18) == 1)
                CallObjectPart(addr::cSC4PropDeveloper_SetTimedProps, self, 0u);
        }
        return 1;
    }
    case 0x278E6003:
    case 0x678E6E3A: {
        // The game reads both and passes neither: the callee's argument
        // points at stack it never wrote, and is not read.
        Virtual<uint32_t>(message, 0x24);
        Virtual<uint32_t>(message, 0x38);
        uint32_t unused[4] = {};
        CallObjectPart(addr::cSC4PropDeveloper_SettleSigns, self, static_cast<void*>(unused));
        return 1;
    }
    case 0x6695681A:
        StepOptionalProps(self, Virtual<uint32_t>(message, 0x4C));
        return 1;
    case 0x66956814: {
        StepOptionalProps(self, 5);
        const uint32_t date = Virtual<uint32_t>(Service(addr::SC4_SimulatorService), 0x1C);
        PtrNode*& list = At<PtrNode*>(self, local::kDatedProps);
        for (PtrNode* node = list->next; node != list; node = node->next) {
            if (node->value)
                Virtual<uint32_t>(node->value, 0x40, date);
        }
        return 1;
    }
    case 0x86AD10EE:
        if (At<uint32_t>(self, local::kChange) == 0)
            return 0;
        CallObjectPart(addr::cSC4PropDeveloper_StepTimedProps, self, Virtual<uint32_t>(message, 0x38) / 1000u);
        return 1;
    case 0x99EF1143:
        if (Virtual<uint32_t>(message, 0x4C) == 0xA823821E)
            CallObjectPart(addr::cSC4PropDeveloper_OnPropRemoved, self, Virtual<void*>(message, 0x28));
        else if (Virtual<uint32_t>(message, 0x4C) == 0xC772BF98)
            CallObjectPart(addr::cSC4PropDeveloper_OnNetworkOccupantRemoved, self, Virtual<void*>(message, 0x28));
        return 1;
    case 0x99EF1142:
        if (Virtual<uint32_t>(message, 0x4C) == 0xA823821E)
            OnPropAdded(self, Virtual<void*>(message, 0x28));
        return 1;
    case 0x8A07AE00: {
        int32_t rect[4];
        rect[3] = Virtual<int32_t>(message, 0x60);
        rect[2] = Virtual<int32_t>(message, 0x4C);
        rect[1] = Virtual<int32_t>(message, 0x38);
        rect[0] = Virtual<int32_t>(message, 0x24);
        CallObjectPart(addr::cSC4PropDeveloper_UpdatePropsInRect, self, static_cast<void*>(rect));
        return 1;
    }
    case 0xAA375FE8: {
        uint32_t value = Virtual<uint32_t>(message, 0x24);
        const uint32_t on = Virtual<uint32_t>(message, 0x38);
        FindResult found{};
        reinterpret_cast<FindFn>(addr::SC4Uint32HashTable_Find)(self + local::kValueTable, &found, &value);
        if (!found.node)
            return 1;
        PtrNode*& list = At<PtrNode*>(found.node, 0x8);
        for (PtrNode* node = list->next; node != list;) {
            if (node->value) {
                Virtual<uint32_t>(node->value, 0x54, 4u, on ? 1u : 0u);
                node = node->next;
            } else {
                PtrNode* const next = node->next;
                node->prev->next = next;
                next->prev = node->prev;
                Free(node);
                node = next;
            }
        }
        return 1;
    }
    case 0xC9DA96E8:
    case 0xC9DA96E9: {
        using ShuffleFn = void(__cdecl*)(void** begin, void** end);
        if (Virtual<uint32_t>(message, 0x10) == 0xC9DA96E9) {
            At<uint32_t>(self, local::kChange) = 1;
            At<uint8_t>(self, local::kState) = 1;
        } else {
            At<uint32_t>(self, local::kChange) = 2;
            At<uint8_t>(self, local::kState) = 0;
        }
        reinterpret_cast<ShuffleFn>(addr::SC4_RandomShuffle)(At<void**>(self, local::kStateProps),
                                                             At<void**>(self, local::kStatePropsEnd));
        const uint32_t props = static_cast<uint32_t>(
            (At<char*>(self, local::kStatePropsEnd) - At<char*>(self, local::kStateProps)) >> 2);
        At<uint32_t>(self, local::kBatches) = props >> 3;
        if (props & 7)
            ++At<uint32_t>(self, local::kBatches);
        At<uint32_t>(self, local::kBatchDone) = 0;
        return 1;
    }
    case 0xCA5E5027: {
        const uint32_t z = Virtual<uint32_t>(message, 0x38);
        const uint32_t x = Virtual<uint32_t>(message, 0x24);
        if (void* const lot = Virtual<void*>(At<void*>(self, local::kLots), 0x18, x, z, 0u))
            CallObjectPart(addr::cSC4PropDeveloper_UpdateLotConditionProps, self, lot);
        return 1;
    }
    case 0xE8BDA2E7: {
        using NewFn = void*(__cdecl*)(std::size_t size);
        using MessageFn = void*(__thiscall*)(void* memory);
        void* const changed = Virtual<void*>(message, 0x80);
        int32_t x = 0;
        int32_t z = 0;
        Virtual<uint32_t>(changed, 0x54, &x, &z);
        uint8_t first = 0;
        uint8_t second = 0;
        Virtual<uint32_t>(changed, 0x58, &first, &second);
        void* const lot = Virtual<void*>(At<void*>(self, local::kLots), 0x18, x, z, 1u);
        if (!lot)
            return 1;
        uint8_t lotFirst = 0;
        uint8_t lotSecond = 0;
        Virtual<uint32_t>(lot, 0x58, &lotFirst, &lotSecond);
        if (first != lotFirst || second != lotSecond)
            return 1;
        if (!Virtual<uint8_t>(changed, 0x94))
            CallObjectPart(addr::cSC4PropDeveloper_UpdateLotOptionalProps, self, lot);
        const uint8_t before = static_cast<uint8_t>(Virtual<uint32_t>(message, 0x24));
        const uint8_t after = static_cast<uint8_t>(Virtual<uint32_t>(message, 0x38));
        void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        void* const sent = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        Virtual<uint32_t>(sent, 0x4);
        Virtual<uint32_t>(sent, 0x14, 0xCA5E5027u);
        Virtual<uint32_t>(sent, 0x2C, x);
        Virtual<uint32_t>(sent, 0x40, z);
        Virtual<uint32_t>(sent, 0x54, before == 2 && after == 3 ? 1u : 0u);
        Virtual<uint32_t>(Service(addr::SC4_MessageServer), 0x10, sent, 0u);
        Virtual<uint32_t>(sent, 0x8);
        return 1;
    }
    default:
        return 1;
    }
}

// --- Installation -----------------------------------------------------------------

// The class's vtables are the object part's entries (data/addresses/prop.toml).
constexpr std::uintptr_t kVtablePrimary = addr::cSC4PropDeveloper_vftable;
constexpr std::uintptr_t kVtableMessages = addr::cSC4PropDeveloper_vftable_4;
constexpr std::uintptr_t kVtableSerializable = addr::cSC4PropDeveloper_vftable_8;

bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours, const char* what)
{
    if (!game::IsBuild641())
        return false;
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("prop %s: slot %d of %08X holds %08X, expected %08X; not installing", what, index,
                   static_cast<unsigned>(vtableAddress), static_cast<unsigned>(*slot),
                   static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    log::Write("prop %s: slot %d of %08X is ours", what, index, static_cast<unsigned>(vtableAddress));
    return true;
}

bool InstallProps(registry::Mode)
{
    const addr::Entry* const update = addr_check::Find(addr::cSC4PropDeveloper_UpdateOptionalProps);
    if (!game::IsBuild641() || !update ||
        !hook::Install(*update, &UpdateOptionalPropsDetour, &g_originalUpdate))
        return false;
    if (!SwapSlot(kVtablePrimary, 7, addr::cSC4PropDeveloper_UpdateAllOptionalProps,
                  reinterpret_cast<void*>(&UpdateAllOptionalProps), "props"))
        return false;
    g_propsOurs = true;
    return true;
}

bool InstallMessages(registry::Mode)
{
    return SwapSlot(kVtableMessages, 3, addr::cSC4PropDeveloper_DoMessage, reinterpret_cast<void*>(&DoMessage),
                    "props messages");
}

bool InstallLoad(registry::Mode)
{
    return SwapSlot(kVtableSerializable, 4, addr::cSC4PropDeveloper_Read, reinterpret_cast<void*>(&Read),
                    "props load");
}

const bool g_registered[] = {
    registry::Register({"simulators.prop.props", "simulators/prop", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_vftable,
                        "Our prop developer optional prop update: the block update (detour), "
                        "UpdateAllOptionalProps, and the step when simulators.prop.props_messages is on too "
                        "(src/core/prop_props.cpp)",
                        false, InstallProps}),
    registry::Register({"simulators.prop.props_messages", "simulators/prop", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_vftable_4,
                        "Our prop developer DoMessage: new props sorted into the lists, the props with hours "
                        "and dates, the step (src/core/prop_props.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.prop.props_load", "simulators/prop", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_vftable_8,
                        "Our prop developer Read, versions 4 and 5 (src/core/prop_props.cpp)", false,
                        InstallLoad}),
};

} // namespace
} // namespace opensc4::prop_sim
