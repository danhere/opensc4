// Our cSC4NetworkTool, second part (build 641; the class map, the cell
// info's and the world cache's layouts are in src/core/nettool_sim.h): the
// world cache (cSC4NetworkWorldCache, the tool's +0x1C) and the cell infos
// it holds. Two groups, each a list of detours at the game's addresses:
// simulators.nettool.cache (the cache's own code, 0x00647830 to 0x00648033
// but for the cell info's three helpers there) and simulators.nettool.cells
// (the cell info's constructor, copy, assignment and edge sum, the tool's
// cell entry 0x00633220 and the makers behind it, slot 71, the RUL0 rule's
// cell marking 0x006097E0 and the two slots of the neighbour object
// 0x00AA8700).
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them.
//
// NAM. Its DLL calls two of these at their addresses, both as __thiscall
// with one argument from compiled C++ (NetworkStubs.h: GetCell 0x00647A20
// with the cache, GetCellInfo 0x00633220 with the tool): the detours keep
// that convention and `ret 4`, and go through naked thunks that leave EAX,
// ECX and EDX as the game's do where the game's are in a known state:
//   - GetCell: EAX the cell info (the node + 8) or 0; ECX the cache (never
//     written); EDX the node or 0; the flags of `test edx, edx` (found) or
//     `xor eax, eax` (not found), as the game's last instructions.
//   - the cell entry: when the cache has the cell, ECX the cache (tool +
//     0x1C) and EDX the node, as GetCell left them, and the flags of the
//     game's `test eax, eax`. When it has not, the maker 0x00633030 is
//     called last from the thunk itself (at its address), so what it leaves
//     comes back as in the game; the game's maker ends with up to two
//     Release calls of occupants, so that state was never a fixed one.
// The register scan of every game caller (76 of 0x00633220, 10 of
// 0x00647A20, following both branches) found none that reads ECX or EDX
// before writing it, nor a call that takes a stale ECX as `this`.
// The cell infos and the cache stay in the game's memory with the game's
// layout: nodes of 0xC4 bytes and vectors of 0xBC-byte cells from the
// game's STL allocator (0x0090CF54, freed by 0x0090CF63), the buckets through
// the game's own vector and hash helpers, temporaries on the stack as the
// game has them; no copy is kept and none of these calls a guarded function.
//
// Two more thunks keep registers the game leaves set, though no caller was
// found that reads them: CombineEdges 0x00647830 (ECX the cell, EDX the cell
// + 0x4C) and the cell info's constructor 0x00647850 (ECX and EDX 0). The
// other functions are plain detours (EAX, ECX, EDX clobbered; EAX as the
// game's where it is an answer).
//
// Left as the game's:
//   - 0x00632EF0 (ret 0xC: x, z, a network type; called by 0x006371E0 and
//     0x006384E0, t9): it makes a cell info for a type on its stack and
//     writes it into the cache, but never sets the second pairs +0x88 to
//     +0xA4, so the cache gets eight words of its stack's leftovers. A
//     replacement cannot write the same, and leaving it the game's does not
//     keep the game's bytes either: they are what the callers' earlier calls
//     (0x00633220 and its maker, ours here) left at that depth. No reader of
//     those pairs is known: slot 71 sets them before it compares.
//   - 0x00629170 (ret 0x18; called by 0x006097E0 only): it appends a
//     0x14-byte record to the tool's vector +0xF0 from a copy on its stack
//     whose last two bytes (+0x12, +0x13) it never writes, so those bytes of
//     the vector are its stack's leftovers.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the cache's code at its addresses: GetCell 0x00647A20 one (ret 4),
//     InsertNode 0x00647A60 one (ret 4), FindOrInsert 0x00647BB0 one (ret 4),
//     FindOrInsertByValue 0x00647E10 one (ret 4), ClearTable 0x006479B0 none
//     (plain ret), AssignTable 0x00647CC0 one (ret 4);
//   - the cell info's: the constructor 0x00647850 none, CombineEdges
//     0x00647830 none, the copy 0x006478A0 one (ret 4), the assignment
//     0x00632D50 one (ret 4), CopyWord 0x00620DD0 one (ret 4, t1);
//   - the tool's: the cell entry 0x00633220 one (ret 4), the maker
//     0x00633030 one (ret 4), GetNetworkOccupantAt 0x00624B70 four (ret
//     0x10, t1), 0x00629170 six (ret 0x18), DirectionOffsets 0x00620D80
//     cdecl three (`add esp, 0xC`, t1);
//   - shared template code: the hash table's Begin 0x004B0390 one (ret 4:
//     the iterator out, node and table), its Resize 0x004F0940 one (ret 4),
//     its construction 0x00829BC0 one (ret 4: the bucket count), the pointer
//     vector's Reserve 0x0045D4F0 one (ret 4) and FillInsert 0x004FBA20 three
//     (ret 0xC: where, the count, the value's address); the allocator
//     0x0090CF54 and the free 0x0090CF63 cdecl one (`add esp, 4`);
//   - the network occupant (the interface 0xA821EF94, vtable 0x00AA8080 of
//     the base): slot 0x20 one (0x00604D10, ret 4), 0x30 none (0x006E3000),
//     0x44 none (0x00604D50), 0x58 one (0x00619520: a tail jump into slot
//     0x20 with the same stack), 0x88 one (0x0065A6C0, ret 4: four words
//     out), 0x98 one (0x0061AF80, a thunk into a `ret 4`), 0x114 none
//     (0x00619940; the game's `push ebp` before it saves EBP, it is not an
//     argument); slot 1 and slot 2 none;
//   - the terrain (+0x44, vtable 0x00AB3D18): slot 0x28 three (0x0074C5E0,
//     ret 0xC: a vertex, x out, z out), slot 0x30 one (0x007413C0, ret 4,
//     the altitude in ST0); the water (0x00B43CF0) slot 0x28 two (x, z; ST0):
//     its `ret 8` is shown by the caller's frame, whose offsets stay the same
//     over its four calls.
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace opensc4::nettool_sim {
namespace {

// The hash table's iterator as Begin 0x004B0390 fills it.
struct Iterator {
    char* node;
    char* table;
};

const Vector* Buckets(const char* table) { return reinterpret_cast<const Vector*>(table + cache::kBuckets); }

// The bucket count as the game computes it: (end - begin) shifted right
// arithmetically by 2.
uint32_t BucketCount(const char* table)
{
    const Vector* const buckets = Buckets(table);
    return static_cast<uint32_t>(static_cast<int32_t>(buckets->end - buckets->begin) >> 2);
}

char*& Bucket(const char* table, uint32_t index) { return reinterpret_cast<char**>(Buckets(table)->begin)[index]; }

// The node after `node` in iteration order (the game's inline increment:
// the next node of the chain, else the first node of the next bucket that
// has one after the key's, else null). `table` is the iterator's table.
char* NextNode(char* node, const char* table)
{
    if (At<char*>(node, node::kNext))
        return At<char*>(node, node::kNext);
    uint32_t index = At<uint32_t>(node, node::kKey) % BucketCount(table);
    char* next = nullptr;
    for (;;) {
        ++index;
        if (index >= BucketCount(table))
            break;
        next = Bucket(table, index);
        if (next)
            break;
    }
    return next;
}

char* Allocate(std::size_t bytes) { return CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, bytes); }
void Free(void* memory) { CallCdecl<void, void*>(addr::SC4_Free, memory); }

// ====================================================================================
// simulators.nettool.cache: the world cache
// ====================================================================================

// 0x00648000 (no arguments; the tool's constructor 0x0062C5F0 at +0x1C):
// for each of the two tables the bucket vector's three words and the count
// cleared, then 0x00829BC0 with 100 buckets. EAX this.
char* __fastcall ConstructCache(char* self, void*)
{
    static bool logged = false;
    FirstCall("ConstructCache", logged);
    for (const std::size_t table : {cache::kMain, cache::kBackup}) {
        At<uint32_t>(self, table + cache::kBuckets) = 0;
        At<uint32_t>(self, table + cache::kBuckets + 4) = 0;
        At<uint32_t>(self, table + cache::kBuckets + 8) = 0;
        At<uint32_t>(self, table + cache::kCount) = 0;
        CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, self + table, cache::kFirstBuckets);
    }
    return self;
}

// 0x006479B0 (no arguments; this a table): every node of every bucket
// freed (its cell's occupant released first, the next node read before
// that), each bucket set to null, then the count cleared. The bucket count
// is computed again after each bucket.
void __fastcall ClearTable(char* table, void*)
{
    static bool logged = false;
    FirstCall("ClearTable", logged);
    for (uint32_t index = 0; index < BucketCount(table); ++index) {
        char* node = Bucket(table, index);
        while (node) {
            void* const occupant = At<void*>(node, node::kValue + cell::kOccupant);
            char* const next = At<char*>(node, node::kNext);
            if (occupant)
                ReleaseOf(occupant);
            Free(node);
            node = next;
        }
        Bucket(table, index) = nullptr;
    }
    At<uint32_t>(table, cache::kCount) = 0;
}

// 0x00647AE0 (no arguments; the tool's destructor 0x0062CAF0): the copy's
// table cleared and its buckets freed, then the same for the cells.
void __fastcall DestructCache(char* self, void*)
{
    static bool logged = false;
    FirstCall("DestructCache", logged);
    char* const backup = self + cache::kBackup;
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, backup);
    if (char* const buckets = Buckets(backup)->begin)
        Free(buckets);
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, self);
    if (char* const buckets = Buckets(self)->begin)
        Free(buckets);
}

// 0x00647B20 (no arguments; Reset 0x00626F90, 0x00629210, 0x0062A5C0, the
// guarded 0x0062CD50 (a caller, not a callee), 0x0062D450, 0x006588D0, and a
// call at 0x0062B121): the
// cells cleared, then the copy (the game's tail jump).
void __fastcall ResetCache(char* self, void*)
{
    static bool logged = false;
    FirstCall("ResetCache", logged);
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, self);
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, self + cache::kBackup);
}

// 0x00647A20 (the thunk's ret 4: the key; NAM calls it; this helper is
// cdecl): the node of the key in the cells' table, key modulo the bucket
// count; the thunk below makes the
// answer (the node + 8, or 0) and the registers.
char* __cdecl FindNode(char* table, uint32_t key)
{
    static bool logged = false;
    FirstCall("GetCell", logged);
    const uint32_t count = BucketCount(table);
    char* node = Bucket(table, key % count);
    while (node && At<uint32_t>(node, node::kKey) != key)
        node = At<char*>(node, node::kNext);
    return node;
}

__declspec(naked) void GetCellThunk()
{
    __asm {
        push ecx
        push dword ptr [esp + 8]
        push ecx
        call FindNode
        add esp, 8
        pop ecx
        mov edx, eax
        test edx, edx
        je missing
        lea eax, [edx + 8]
        test edx, edx
        ret 4
    missing:
        xor eax, eax
        ret 4
    }
}

// 0x00647A60 (ret 4: the address of a key and a cell info, as one pair;
// FindOrInsert's only): the table grown for one more (0x004F0940 with the
// count + 1), the bucket of the key (after the growth), a node of 0xC4
// bytes with no next, the key and a copy of the cell info (0x006478A0), put
// first in its bucket; the count + 1. EAX the node + 4 (the pair).
char* __fastcall InsertNode(char* table, void*, const char* pair)
{
    static bool logged = false;
    FirstCall("InsertNode", logged);
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, table, At<uint32_t>(table, cache::kCount) + 1);
    const uint32_t index = At<uint32_t>(const_cast<char*>(pair), 0) % BucketCount(table);
    char* const head = Bucket(table, index);
    char* const fresh = Allocate(node::kSize);
    At<char*>(fresh, node::kNext) = nullptr;
    At<uint32_t>(fresh, node::kKey) = At<uint32_t>(const_cast<char*>(pair), 0);
    CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, fresh + node::kValue, pair + 4);
    At<char*>(fresh, node::kNext) = head;
    Bucket(table, index) = fresh;
    At<uint32_t>(table, cache::kCount) = At<uint32_t>(table, cache::kCount) + 1;
    return fresh + node::kKey;
}

// 0x00647BB0 (ret 4: the key's address; 0x00647E10's): the cell info of the
// key, made when the table has none: a pair of the key and a copy
// (0x006478A0) of a default cell info, inserted (0x00647A60); then the two
// temporaries' occupants released (both null here, as the game's flags
// show). EAX the cell info.
//
// The default cell info is built inline: the occupant, the edge words, the
// combined word and the byte +0x50 cleared and the index -1, as the
// constructor 0x00647850 does. The game leaves its other words as its
// stack had them (the copy writes them into the node); ours are 0. Every
// game caller of 0x00647E10 either assigns a whole cell info over the
// answer at once (0x00632EF0, 0x00633030, slot 71, 0x00657DA0) or looks up
// keys it inserted just before (0x006588D0 at 0x00658CED and 0x00658EBC).
char* __fastcall FindOrInsert(char* table, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("FindOrInsert", logged);
    const uint32_t wanted = *key;
    char* found = Bucket(table, wanted % BucketCount(table));
    while (found && At<uint32_t>(found, node::kKey) != wanted)
        found = At<char*>(found, node::kNext);
    if (found)
        return found + node::kValue;
    alignas(4) char empty[cell::kSize] = {};
    for (std::size_t i = 0; i < cell::kEdgeWords; ++i)
        At<uint32_t>(empty, cell::kByType + 4 * i) = 0;
    At<uint8_t>(empty, cell::kOneWay) = 0;
    At<void*>(empty, cell::kOccupant) = nullptr;
    At<uint32_t>(empty, cell::kCombined) = 0;
    At<int32_t>(empty, cell::kSolvedIndex) = -1;
    alignas(4) char pair[4 + cell::kSize];
    At<uint32_t>(pair, 0) = *key;
    CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, pair + 4, empty);
    char* const inserted = CallThis<char*, const char*>(addr::cSC4NetworkWorldCache_InsertNode, table, pair);
    char* const value = inserted + 4;
    if (void* const held = At<void*>(pair, 4 + cell::kOccupant))
        ReleaseOf(held);
    if (void* const held = At<void*>(empty, cell::kOccupant))
        ReleaseOf(held);
    return value;
}

// 0x00647E10 (ret 4: the key by value; 0x00632EF0, 0x00633030, slot 71,
// 0x00657DA0, 0x006588D0): 0x00647BB0 with the key's address. EAX its.
char* __fastcall FindOrInsertByValue(char* table, void*, uint32_t key)
{
    static bool logged = false;
    FirstCall("FindOrInsertByValue", logged);
    return CallThis<char*, const uint32_t*>(addr::cSC4NetworkWorldCache_FindOrInsert, table, &key);
}

// 0x00647B40 (no arguments; this the cells' table; 0x0063B830 calls it):
// every cell's index into the solved cells (+0xB8) set to -1, walking from
// Begin (0x004B0390) in the game's order.
void __fastcall ClearCellIndices(char* table, void*)
{
    static bool logged = false;
    FirstCall("ClearCellIndices", logged);
    Iterator at{nullptr, nullptr};
    CallThis<void*, Iterator*>(addr::SC4HashTable_Begin, table, &at);
    char* node = at.node;
    while (node) {
        At<int32_t>(node, node::kValue + cell::kSolvedIndex) = -1;
        node = NextNode(node, at.table);
    }
}

// 0x00647CC0 (ret 4: the other table; this a table that is empty of nodes:
// both callers clear it first): the bucket vector emptied (end = begin; the
// game's erase of the whole range, whose element move is dead code),
// reserved (0x0045D4F0) and filled (0x004FBA20) with as many null buckets
// as the other has; each of the other's chains copied in its order into new
// 0xC4-byte nodes (the key and the cell info's copy 0x006478A0); the count
// copied. The bucket count of the other is computed again for each bucket.
void __fastcall AssignTable(char* self, void*, char* other)
{
    static bool logged = false;
    FirstCall("AssignTable", logged);
    Vector* const buckets = reinterpret_cast<Vector*>(self + cache::kBuckets);
    buckets->end = buckets->begin;
    CallThis<void, uint32_t>(addr::PtrVector_Reserve, buckets, BucketCount(other));
    const uint32_t none = 0;
    CallThis<void, char*, uint32_t, const uint32_t*>(addr::SC4PtrVector_FillInsert, buckets, buckets->end,
                                                     BucketCount(other), &none);
    for (uint32_t index = 0; index < BucketCount(other); ++index) {
        char* from = Bucket(other, index);
        if (!from)
            continue;
        char* const first = Allocate(node::kSize);
        At<char*>(first, node::kNext) = nullptr;
        At<uint32_t>(first, node::kKey) = At<uint32_t>(from, node::kKey);
        CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, first + node::kValue,
                                     from + node::kValue);
        Bucket(self, index) = first;
        char* last = first;
        for (from = At<char*>(from, node::kNext); from; from = At<char*>(from, node::kNext)) {
            char* const copy = Allocate(node::kSize);
            At<char*>(copy, node::kNext) = nullptr;
            At<uint32_t>(copy, node::kKey) = At<uint32_t>(from, node::kKey);
            CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, copy + node::kValue,
                                         from + node::kValue);
            At<char*>(last, node::kNext) = copy;
            last = copy;
        }
    }
    At<uint32_t>(self, cache::kCount) = At<uint32_t>(other, cache::kCount);
}

// 0x00647E20 (no arguments; 0x0063B830): unless the copy is the cells
// (never), the cells cleared and the copy assigned to them.
void __fastcall RestoreCells(char* self, void*)
{
    static bool logged = false;
    FirstCall("RestoreCells", logged);
    char* const backup = self + cache::kBackup;
    if (backup == self)
        return;
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, self);
    CallThis<void, char*>(addr::cSC4NetworkWorldCache_AssignTable, self, backup);
}

// 0x00647E40 (no arguments; 0x0063B830): the copy cleared and the cells
// assigned to it.
void __fastcall SaveCells(char* self, void*)
{
    static bool logged = false;
    FirstCall("SaveCells", logged);
    char* const backup = self + cache::kBackup;
    if (self == backup)
        return;
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearTable, backup);
    CallThis<void, char*>(addr::cSC4NetworkWorldCache_AssignTable, backup, self);
}

// 0x00647E60 (ret 4: a vector of 0xBC-byte cell infos; this the cells'
// table; the underground tools' 0x00658710 calls it): every cell appended
// in the game's iteration order, copy constructed (0x006478A0). With room
// at the end, in place; else new memory for the size plus the larger of the
// size and 1 (0 bytes never asked for: null), the old cells copied, the new
// one copied after them, the old ones' occupants released and their memory
// freed. The size is the game's signed division by 0xBC.
void __fastcall CollectCells(char* table, void*, Vector* out)
{
    static bool logged = false;
    FirstCall("CollectCells", logged);
    Iterator at{nullptr, nullptr};
    CallThis<void*, Iterator*>(addr::SC4HashTable_Begin, table, &at);
    char* node = at.node;
    while (node) {
        char* const value = node + node::kValue;
        if (out->end != out->capacity) {
            if (out->end)
                CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, out->end, value);
            out->end += cell::kSize;
        } else {
            char* const oldEnd = out->end;
            const uint32_t size =
                static_cast<uint32_t>(static_cast<int32_t>(oldEnd - out->begin) / static_cast<int32_t>(cell::kSize));
            const uint32_t capacity = size + (size < 1 ? 1u : size);
            char* const memory = capacity ? Allocate(capacity * cell::kSize) : nullptr;
            char* to = memory;
            for (char* from = out->begin; from != oldEnd; from += cell::kSize, to += cell::kSize)
                if (to)
                    CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, to, from);
            if (to)
                CallThis<char*, const char*>(addr::cSC4NetworkCellInfo_CopyConstruct, to, value);
            char* const newEnd = to + cell::kSize;
            char* const end = out->end;
            for (char* old = out->begin; old != end; old += cell::kSize)
                if (void* const occupant = At<void*>(old, cell::kOccupant))
                    ReleaseOf(occupant);
            if (out->begin)
                Free(out->begin);
            out->begin = memory;
            out->end = newEnd;
            out->capacity = memory + capacity * cell::kSize;
        }
        node = NextNode(node, at.table);
    }
}

// ====================================================================================
// simulators.nettool.cells: the cell infos and the tool's code that makes
// them
// ====================================================================================

// 0x00647830 (no arguments; this a cell info; 14 callers): the 13 edge words
// ORed into +0x4C. EAX the result; the thunk leaves ECX the cell and EDX
// the cell + 0x4C, as the game's loop does.
uint32_t __cdecl CombineEdges(char* self)
{
    static bool logged = false;
    FirstCall("CombineEdges", logged);
    uint32_t combined = 0;
    for (std::size_t i = 0; i < cell::kEdgeWords; ++i)
        combined |= At<uint32_t>(self, cell::kByType + 4 * i);
    At<uint32_t>(self, cell::kCombined) = combined;
    return combined;
}

__declspec(naked) void CombineEdgesThunk()
{
    __asm {
        push ecx
        push ecx
        call CombineEdges
        add esp, 4
        pop ecx
        lea edx, [ecx + 0x4C]
        ret
    }
}

// 0x00647850 (no arguments; the cell info's constructor): the occupant,
// the 13 edge words, the combined word and the byte +0x50 cleared, the index
// +0xB8 set to -1; nothing else. EAX this; the thunk leaves ECX and EDX 0
// as the game's.
char* __cdecl ConstructCellInfo(char* self)
{
    static bool logged = false;
    FirstCall("ConstructCellInfo", logged);
    At<void*>(self, cell::kOccupant) = nullptr;
    for (std::size_t i = 0; i < cell::kEdgeWords; ++i)
        At<uint32_t>(self, cell::kByType + 4 * i) = 0;
    At<uint32_t>(self, cell::kCombined) = 0;
    At<uint8_t>(self, cell::kOneWay) = 0;
    At<int32_t>(self, cell::kSolvedIndex) = -1;
    return self;
}

__declspec(naked) void ConstructCellInfoThunk()
{
    __asm {
        push ecx
        call ConstructCellInfo
        add esp, 4
        xor ecx, ecx
        xor edx, edx
        ret
    }
}

// 0x006478A0 (ret 4: the cell info to copy; this the new one): the words
// +0x0 to +0x8, the occupant (referenced through slot 1 after it is
// stored, when not null), the byte +0x10, the word +0x14, the 13 edge words
// and the combined word each by CopyWord 0x00620DD0 (at its address), the
// bytes +0x50 to +0x57 and the words +0x58 to +0xB8. The padding +0x11 to
// +0x13 is not copied. EAX this.
char* __fastcall CopyConstructCellInfo(char* self, void*, char* from)
{
    static bool logged = false;
    FirstCall("CopyConstructCellInfo", logged);
    At<uint32_t>(self, cell::kX) = At<uint32_t>(from, cell::kX);
    At<uint32_t>(self, cell::kZ) = At<uint32_t>(from, cell::kZ);
    At<uint32_t>(self, cell::kFlags) = At<uint32_t>(from, cell::kFlags);
    void* const occupant = At<void*>(from, cell::kOccupant);
    At<void*>(self, cell::kOccupant) = occupant;
    if (occupant)
        AddRefOf(occupant);
    At<uint8_t>(self, cell::kInPlacement) = At<uint8_t>(from, cell::kInPlacement);
    At<uint32_t>(self, cell::kStep) = At<uint32_t>(from, cell::kStep);
    for (std::size_t i = 0; i < cell::kEdgeWords; ++i)
        CallThis<uint32_t*, char*>(addr::cSC4NetworkTool_CopyWord, self + cell::kByType + 4 * i,
                                   from + cell::kByType + 4 * i);
    CallThis<uint32_t*, char*>(addr::cSC4NetworkTool_CopyWord, self + cell::kCombined, from + cell::kCombined);
    for (std::size_t offset = cell::kOneWay; offset <= cell::kByte57; ++offset)
        At<uint8_t>(self, offset) = At<uint8_t>(from, offset);
    for (std::size_t offset = cell::kHeights; offset <= cell::kSolvedIndex; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(from, offset);
    return self;
}

// 0x00632D50 (ret 4: the cell info to assign; 0x00632EF0, 0x00633030, slot
// 71, 0x00657DA0, 0x006588D0): the words +0x0 to +0x8; the occupant unless
// the two are the same object: when it differs, the new one referenced
// (when not null), stored, the old one released (when not null); the byte
// +0x10, the word +0x14, the 13 edge words, the combined word, the bytes
// +0x50 to +0x57, the words +0x58 to +0xB8. EAX this.
char* __fastcall AssignCellInfo(char* self, void*, char* from)
{
    static bool logged = false;
    FirstCall("AssignCellInfo", logged);
    At<uint32_t>(self, cell::kX) = At<uint32_t>(from, cell::kX);
    At<uint32_t>(self, cell::kZ) = At<uint32_t>(from, cell::kZ);
    At<uint32_t>(self, cell::kFlags) = At<uint32_t>(from, cell::kFlags);
    if (from != self) {
        void* const old = At<void*>(self, cell::kOccupant);
        void* const fresh = At<void*>(from, cell::kOccupant);
        if (fresh != old) {
            if (fresh)
                AddRefOf(fresh);
            At<void*>(self, cell::kOccupant) = fresh;
            if (old)
                ReleaseOf(old);
        }
    }
    At<uint8_t>(self, cell::kInPlacement) = At<uint8_t>(from, cell::kInPlacement);
    At<uint32_t>(self, cell::kStep) = At<uint32_t>(from, cell::kStep);
    for (std::size_t i = 0; i < cell::kEdgeWords; ++i)
        At<uint32_t>(self, cell::kByType + 4 * i) = At<uint32_t>(from, cell::kByType + 4 * i);
    At<uint32_t>(self, cell::kCombined) = At<uint32_t>(from, cell::kCombined);
    for (std::size_t offset = cell::kOneWay; offset <= cell::kByte57; ++offset)
        At<uint8_t>(self, offset) = At<uint8_t>(from, offset);
    for (std::size_t offset = cell::kHeights; offset <= cell::kSolvedIndex; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(from, offset);
    return self;
}

// The four vertex indices of the cell (x, z) with `stride` vertices per
// row, as the makers store them: NW, SW, SE, NE.
void StoreVertices(char* info, uint32_t stride, uint32_t x, uint32_t z)
{
    const uint32_t first = stride * z + x;
    const uint32_t below = stride + first;
    At<uint32_t>(info, cell::kVertices) = first;
    At<uint32_t>(info, cell::kVertices + 4) = below;
    At<uint32_t>(info, cell::kVertices + 8) = below + 1;
    At<uint32_t>(info, cell::kVertices + 12) = first + 1;
}

// 0x00633030 (ret 4: the key; this the tool; the cell entry 0x00633220 and
// slot 71 call it): a cell info made on the stack (0x00647850), its x and z
// from the key and its vertex indices; the cell's network occupant through
// 0x00624B70 (with the filter +0x364; its answer is not read, only the
// out pointer, cleared before). None: the temporary's occupant released
// (null) and the answer is 0. Else: for each type 0 to 12 the occupant
// asked whether it is of it (slot 0x58) and its edges for it (slot 0x98);
// the byte +0x50 its slot 0x114; it is stored as the occupant (referenced);
// the flags its slot 0x30 & 0x1FFF; +0x10 0, +0x14 -1, +0x51 1, +0x52 slot
// 0x20 with 0x100000, +0x53 slot 0x44, +0x54 and +0x55 0, +0x56 slot 0x20
// with 0x200000, +0x57 0, +0xB8 -1; the corner heights from slot 0x88; the
// eight pairs -FLT_MAX, FLT_MAX; the edges combined (0x00647830); then the
// cache's cell of the key (0x00647E10, made when missing) assigned from it
// (0x00632D50), the occupant released and the temporary's occupant
// released. EAX the cell in the cache.
char* __fastcall MakeCellInfo(char* self, void*, uint32_t key)
{
    static bool logged = false;
    FirstCall("MakeCellInfo", logged);
    alignas(4) char info[cell::kSize];
    CallThis<char*>(addr::cSC4NetworkCellInfo_Construct, info);
    const uint32_t z = key >> 16;
    const uint32_t x = key & 0xFFFF;
    StoreVertices(info, At<uint32_t>(self, field::kVertexStride), x, z);
    At<uint32_t>(info, cell::kX) = x;
    At<uint32_t>(info, cell::kZ) = z;
    void* occupant = nullptr;
    CallThis<uint8_t, uint32_t, uint32_t, void*, void**>(addr::cSC4NetworkTool_GetNetworkOccupantAt, self, x, z,
                                                          At<void*>(self, field::kFilter), &occupant);
    if (!occupant) {
        if (void* const held = At<void*>(info, cell::kOccupant))
            ReleaseOf(held);
        return nullptr;
    }
    for (uint32_t type = 0; type < kTypes; ++type)
        if (Virtual<uint8_t>(occupant, netocc_slot::kIsOfType, type))
            At<uint32_t>(info, cell::kByType + 4 * type) = Virtual<uint32_t>(occupant, netocc_slot::kEdges, type);
    At<uint8_t>(info, cell::kOneWay) = Virtual<uint8_t>(occupant, netocc_slot::kOneWay);
    void* const held = At<void*>(info, cell::kOccupant);
    if (occupant != held) {
        AddRefOf(occupant);
        At<void*>(info, cell::kOccupant) = occupant;
        if (held)
            ReleaseOf(held);
    }
    At<uint32_t>(info, cell::kFlags) = Virtual<uint32_t>(occupant, netocc_slot::kFlags) & cell::kOccupantFlagMask;
    At<uint8_t>(info, cell::kInPlacement) = 0;
    At<int32_t>(info, cell::kStep) = -1;
    At<uint8_t>(info, cell::kOccupied) = 1;
    At<uint8_t>(info, cell::kHasFlag100000) =
        Virtual<uint8_t>(occupant, netocc_slot::kHasFlag, cell::kOccupantFlag100000);
    At<uint8_t>(info, cell::kImmovable) = Virtual<uint8_t>(occupant, netocc_slot::kIsImmovable);
    At<uint8_t>(info, cell::kByte54) = 0;
    At<uint8_t>(info, cell::kKind3) = 0;
    At<uint8_t>(info, cell::kNetworkLot) =
        Virtual<uint8_t>(occupant, netocc_slot::kHasFlag, cell::kOccupantFlagNetworkLot);
    At<uint8_t>(info, cell::kByte57) = 0;
    At<int32_t>(info, cell::kSolvedIndex) = -1;
    Virtual<void>(occupant, netocc_slot::kVertexHeights, info + cell::kHeights);
    for (std::size_t corner = 0; corner < 4; ++corner) {
        At<uint32_t>(info, cell::kRanges + 8 * corner) = cell::kLowestBits;
        At<uint32_t>(info, cell::kRanges + 8 * corner + 4) = cell::kHighestBits;
        At<uint32_t>(info, cell::kRanges2 + 8 * corner) = cell::kLowestBits;
        At<uint32_t>(info, cell::kRanges2 + 8 * corner + 4) = cell::kHighestBits;
    }
    CallThis<uint32_t>(addr::cSC4NetworkCellInfo_CombineEdges, info);
    char* const cached =
        CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_FindOrInsertByValue, self + field::kWorldCache, key);
    CallThis<char*, char*>(addr::cSC4NetworkCellInfo_Assign, cached, info);
    ReleaseOf(occupant);
    if (void* const mine = At<void*>(info, cell::kOccupant))
        ReleaseOf(mine);
    return cached;
}

// 0x00633220 (ret 4: the key (z << 16) + x; this the tool; NAM calls it;
// 76 callers): the cache's cell (0x00647A20 at its address) or, without
// one, a new one from the cell's occupant (0x00633030 at its address, which
// answers 0 when the cell has no network occupant). The thunk calls the
// maker itself, last, as the game does (this the tool, the key), so that
// the registers it leaves come back as in the game.
char* __cdecl CellEntryFind(char* self, uint32_t key)
{
    static bool logged = false;
    FirstCall("CellEntry", logged);
    return CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_GetCell, self + field::kWorldCache, key);
}

// The maker's address, for the thunk's call.
std::uintptr_t g_makeCellInfo = addr::cSC4NetworkTool_MakeCellInfo;

__declspec(naked) void CellEntryThunk()
{
    __asm {
        push esi
        push edi
        mov edi, dword ptr [esp + 0xC]
        mov esi, ecx
        push edi
        push esi
        call CellEntryFind
        add esp, 8
        test eax, eax
        jne found
        push edi
        mov ecx, esi
        call dword ptr [g_makeCellInfo]
        jmp done
    found:
        lea ecx, [esi + 0x1C]
        lea edx, [eax - 8]
    done:
        pop edi
        pop esi
        ret 4
    }
}

// The water at a cell's four corners raises its two low bounds (slot 71's
// common tail): for each corner the terrain's slot 0x28 gives the vertex's
// x and z, the water's slot 0x28 a height (ST0) that replaces the first
// pair's low (+0x68) and then the second pair's low (+0x88) where it is
// greater (an unordered compare keeps the old), stored as a float; the
// comparisons use the unrounded height. The terrain is read for each
// corner, the water once.
void RaiseToWater(char* self, char* info)
{
    void* const water = Global<void*>(addr::SC4_Service43CF0);
    for (std::size_t corner = 0; corner < 4; ++corner) {
        int32_t x = 0, z = 0;
        Virtual<void>(At<void*>(self, field::kTerrain), terrain_slot::kVertexCell,
                      At<uint32_t>(info, cell::kVertices + 4 * corner), &x, &z);
        const double height = Virtual<double>(water, water_slot::kAltitude, x, z);
        float& low = At<float>(info, cell::kRanges + 8 * corner);
        if (height > static_cast<double>(low))
            low = static_cast<float>(height);
        float& low2 = At<float>(info, cell::kRanges2 + 8 * corner);
        if (height > static_cast<double>(low2))
            low2 = static_cast<float>(height);
    }
}

// Slot 71 (0x00633250, ret 4: a network type; the general and the power
// line tool): for each valid step (signed, up to +0x78, read again after
// each) and each of its dragged cells from the first to the last (unsigned,
// the last read again after each), the cell's info: the cache's (0x00647A20)
// or one made from its occupant (0x00633030), both at their addresses.
//   - A cell info: the type's flag (0x00AA8980) ORed into its flags, +0x10
//     set, +0x14 the step's index, +0x53 set when the step's kind (its third
//     word) is not 0; for each corner the pair +0x68 the height less, and
//     +0x6C the height plus, the margin at +0x2A4 (the parameters' +0x14;
//     double sums stored as floats), the pair +0x88 the height twice.
//   - None (no network occupant there): a new one made on the stack
//     (0x00647850) with x, z, the type's flag, +0x10 1, +0x14 the step's
//     index, +0x51, +0x52, +0x54, +0x56, +0x57 0, +0x53 the kind not 0, +0x55
//     the kind is 3, the vertex indices and +0xB8 -1; for each corner the
//     terrain's altitude (slot 0x30, ST0) stored as the height, the pair
//     +0x68 the altitude less the margin at +0x2C4 (the parameters' +0x34;
//     the unrounded altitude) and the stored height plus it, the pair +0x88
//     the constants 0xFDFFFFFF and 0x7DFFFFFF; put in the cache (0x00647E10
//     and 0x00632D50); the temporary's occupant (null) released.
// Then the step's kind 1 ORs 0x40000 into the flags, kind 2 0x80000, and
// the water raises the low bounds (RaiseToWater).
void __fastcall CreateCellInfosForCurrentPlacementTiles(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("CreateCellInfosForCurrentPlacementTiles", logged);
    if (At<int32_t>(self, field::kValidSteps) <= 0)
        return;
    std::size_t offset = 0;
    for (int32_t index = 0;;) {
        uint32_t at = At<uint32_t>(At<char*>(self, field::kSteps), offset);
        if (at <= At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kLastCell)) {
            for (;;) {
                char* const cells = At<char*>(self, field::kCells);
                const uint32_t z = At<uint32_t>(cells, at * kPointSize + 4);
                const uint32_t x = At<uint32_t>(cells, at * kPointSize);
                const uint32_t key = (z << 16) + x;
                char* info =
                    CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_GetCell, self + field::kWorldCache, key);
                if (!info)
                    info = CallThis<char*, uint32_t>(addr::cSC4NetworkTool_MakeCellInfo, self, key);
                if (info) {
                    At<uint32_t>(info, cell::kFlags) |=
                        Global<uint32_t>(addr::cSC4NetworkTool_CellTypeFlags + type * 4);
                    At<uint8_t>(info, cell::kInPlacement) = 1;
                    At<int32_t>(info, cell::kStep) = index;
                    if (At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kIrregular) != 0)
                        At<uint8_t>(info, cell::kImmovable) = 1;
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        const double height = At<float>(info, cell::kHeights + 4 * corner);
                        const double margin = At<float>(self, field::kMarginCached);
                        At<float>(info, cell::kRanges + 8 * corner) = static_cast<float>(height - margin);
                        At<float>(info, cell::kRanges + 8 * corner + 4) = static_cast<float>(height + margin);
                        At<float>(info, cell::kRanges2 + 8 * corner + 4) = At<float>(info, cell::kHeights + 4 * corner);
                        At<float>(info, cell::kRanges2 + 8 * corner) = At<float>(info, cell::kHeights + 4 * corner);
                    }
                } else {
                    alignas(4) char fresh[cell::kSize];
                    CallThis<char*>(addr::cSC4NetworkCellInfo_Construct, fresh);
                    const uint32_t kind = At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kIrregular);
                    At<uint32_t>(fresh, cell::kStep) = static_cast<uint32_t>(index);
                    At<uint32_t>(fresh, cell::kFlags) =
                        Global<uint32_t>(addr::cSC4NetworkTool_CellTypeFlags + type * 4);
                    At<uint32_t>(fresh, cell::kX) = x;
                    At<uint32_t>(fresh, cell::kZ) = z;
                    At<uint8_t>(fresh, cell::kInPlacement) = 1;
                    At<uint8_t>(fresh, cell::kOccupied) = 0;
                    At<uint8_t>(fresh, cell::kHasFlag100000) = 0;
                    At<uint8_t>(fresh, cell::kImmovable) = kind != 0 ? 1 : 0;
                    At<uint8_t>(fresh, cell::kByte54) = 0;
                    At<uint8_t>(fresh, cell::kKind3) = kind == 3 ? 1 : 0;
                    StoreVertices(fresh, At<uint32_t>(self, field::kVertexStride), x, z);
                    At<uint8_t>(fresh, cell::kNetworkLot) = 0;
                    At<uint8_t>(fresh, cell::kByte57) = 0;
                    At<int32_t>(fresh, cell::kSolvedIndex) = -1;
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        const double altitude =
                            Virtual<double>(At<void*>(self, field::kTerrain), terrain_slot::kVertexAltitude,
                                            At<uint32_t>(fresh, cell::kVertices + 4 * corner));
                        At<float>(fresh, cell::kHeights + 4 * corner) = static_cast<float>(altitude);
                        const double margin = At<float>(self, field::kMarginNew);
                        At<float>(fresh, cell::kRanges + 8 * corner) = static_cast<float>(altitude - margin);
                        At<uint32_t>(fresh, cell::kRanges2 + 8 * corner) = cell::kNewLowBits;
                        At<float>(fresh, cell::kRanges + 8 * corner + 4) = static_cast<float>(
                            static_cast<double>(At<float>(fresh, cell::kHeights + 4 * corner)) + margin);
                        At<uint32_t>(fresh, cell::kRanges2 + 8 * corner + 4) = cell::kNewHighBits;
                    }
                    info = CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_FindOrInsertByValue,
                                                     self + field::kWorldCache, key);
                    CallThis<char*, char*>(addr::cSC4NetworkCellInfo_Assign, info, fresh);
                    if (void* const held = At<void*>(fresh, cell::kOccupant))
                        ReleaseOf(held);
                }
                const uint32_t kind = At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kIrregular);
                if (kind == 1)
                    At<uint32_t>(info, cell::kFlags) |= cell::kStepKind1Flag;
                else if (kind == 2)
                    At<uint32_t>(info, cell::kFlags) |= cell::kStepKind2Flag;
                RaiseToWater(self, info);
                ++at;
                if (at > At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kLastCell))
                    break;
            }
        }
        ++index;
        offset += step::kSize;
        if (index >= At<int32_t>(self, field::kValidSteps))
            break;
    }
}

// 0x006097E0 (ret 0xC: an x offset, a z offset, the tool; this a RUL0 rule;
// the network view's controls 0x00661010 and 0x00661260 call it): for each
// of the rule's cells, its byte's entry in the rule's hash table (the game
// reads a missing entry through null, as ours does), and for each of the
// entry's two network types that is not negative: the tool's cell info at
// the offset cell (0x00633220: found or made) and whether its edge word for
// the type differs from the entry's pattern under the entry's mask (a
// missing cell counts as differing). For the type 10 (inf: one-way roads):
// the four sides' bits of the pattern ((w & 0x03030303) + 0x03030303) &
// 0x04040404 gathered by * 0xC30C30 >> 24, compared with the cell's byte
// +0x50 (0 without a cell) against the rule's rotation byte (0x00AA752C by
// the rule's +0xE8), and unless the entry's byte +0x1D is set, a cell whose
// directions (the byte, its neighbours' bits and itself) cover the bits is
// passed over. A cell that is there, matches and is not blocked is passed
// over; any other gets a record (0x00629170 at its address: the cell, the
// pattern, the type, the bits under the rotation byte, the bits). AL 1.
uint8_t __fastcall MarkRuleCells(char* self, void*, uint32_t dx, uint32_t dz, char* tool)
{
    static bool logged = false;
    FirstCall("MarkRuleCells", logged);
    const uint8_t rotation =
        Global<uint8_t>(addr::cSC4NetworkTool_RuleRotationMasks + At<uint8_t>(self, rule::kRotation));
    const Vector* const cells = reinterpret_cast<const Vector*>(self + rule::kCells);
    char* at = cells->begin;
    char* const end = cells->end;
    if (at == end)
        return 1;
    do {
        const Vector* const buckets = reinterpret_cast<const Vector*>(self + rule::kBuckets);
        const uint8_t key = At<uint8_t>(at, rule::kCellKey);
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(buckets->end - buckets->begin) >> 2);
        char* entry = reinterpret_cast<char**>(buckets->begin)[key % count];
        while (entry && At<uint8_t>(entry, rule::kNodeKey) != key)
            entry = At<char*>(entry, node::kNext);
        for (uint32_t which = 0; which < 2; ++which) {
            const int8_t type = At<int8_t>(entry, rule::kNodeTypes + which);
            if (type < 0)
                continue;
            const uint32_t* const pattern = reinterpret_cast<const uint32_t*>(entry + rule::kNodePairs + 8 * which);
            const uint32_t x = At<uint32_t>(at, 0) + dx;
            const uint32_t z = At<uint32_t>(at, 4) + dz;
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, tool, (z << 16) + x);
            uint8_t differs = 1;
            if (info)
                differs = ((At<uint32_t>(info, cell::kByType + 4 * static_cast<std::size_t>(type)) ^ pattern[0]) &
                           pattern[1]) != 0
                              ? 1
                              : 0;
            const int8_t kind = At<int8_t>(entry, rule::kNodeTypes + which);
            uint8_t bits = 0;
            uint8_t blocked = 0;
            if (kind == rule::kOneWayType) {
                const uint8_t oneWay = info ? At<uint8_t>(info, cell::kOneWay) : 0;
                uint32_t sides = pattern[0];
                sides = ((sides & 0x03030303u) + 0x03030303u) & 0x04040404u;
                sides *= 0x00C30C30u;
                bits = static_cast<uint8_t>(sides >> 24);
                const uint8_t against = static_cast<uint8_t>(oneWay ^ rotation);
                blocked = (bits & against) != 0 ? 1 : 0;
                if (At<uint8_t>(entry, rule::kNodeByte1D) == 0) {
                    uint8_t around = static_cast<uint8_t>((oneWay >> 1) ^ static_cast<uint8_t>(oneWay << 1));
                    around = static_cast<uint8_t>(around & 0x55);
                    around = static_cast<uint8_t>(around ^ static_cast<uint8_t>(oneWay << 1));
                    around = static_cast<uint8_t>(around | oneWay);
                    around = static_cast<uint8_t>(around & against);
                    if (bits & around)
                        continue;
                }
            }
            if (info && !differs && !blocked)
                continue;
            CallThis<void, uint32_t, uint32_t, uint32_t, int32_t, uint32_t, uint32_t>(
                addr::cSC4NetworkTool_AddRuleCellRecord, tool, x, z, pattern[0], static_cast<int32_t>(kind),
                static_cast<uint32_t>(bits & rotation), static_cast<uint32_t>(bits));
        }
        at += rule::kCellSize;
    } while (at != end);
    return 1;
}

// The neighbour of the walker's cell one step in a direction (the step
// tables through DirectionOffsets 0x00620D80 at its address; the bytes are
// signed): its key.
uint32_t NeighborKey(char* self, uint32_t direction)
{
    uint8_t stepX = 0, stepZ = 0;
    CallCdecl<uint32_t, uint32_t, uint8_t*, uint8_t*>(addr::cSC4NetworkTool_DirectionOffsets, direction, &stepX,
                                                      &stepZ);
    const uint32_t x = At<uint32_t>(self, walker::kX) + static_cast<uint32_t>(static_cast<int8_t>(stepX));
    const uint32_t z = At<uint32_t>(self, walker::kZ) + static_cast<uint32_t>(static_cast<int8_t>(stepZ));
    return (z << 16) + x;
}

// Slot 0 of the vtable 0x00AA8700 (0x00623F40, ret 4: a direction): 0 when
// the neighbour's cell info (0x00633220 with the walker's tool) has a flag
// of the walker's mask, else 1 (all of EAX).
uint32_t __fastcall NeighborIsClear(char* self, void*, uint32_t direction)
{
    static bool logged = false;
    FirstCall("NeighborIsClear", logged);
    const uint32_t key = NeighborKey(self, direction);
    char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry,
                                                 At<char*>(self, walker::kTool), key);
    if (info && (At<uint32_t>(self, walker::kMask) & At<uint32_t>(info, cell::kFlags)))
        return 0;
    return 1;
}

// Slot 1 (0x00623EE0, ret 8: a direction, a word out): the word cleared,
// then the neighbour's edge word for the walker's type when it has a cell
// info. EAX the cell info or 0.
char* __fastcall NeighborEdges(char* self, void*, uint32_t direction, uint32_t* edges)
{
    static bool logged = false;
    FirstCall("NeighborEdges", logged);
    const uint32_t key = NeighborKey(self, direction);
    *edges = 0;
    char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry,
                                                 At<char*>(self, walker::kTool), key);
    if (info)
        *edges = At<uint32_t>(info, cell::kByType + 4 * At<uint32_t>(self, walker::kType));
    return info;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kCache[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_Construct, ConstructCache),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_Destruct, DestructCache),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_Reset, ResetCache),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_ClearTable, ClearTable),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_GetCell, GetCellThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_InsertNode, InsertNode),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_FindOrInsert, FindOrInsert),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_FindOrInsertByValue, FindOrInsertByValue),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_ClearCellIndices, ClearCellIndices),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_AssignTable, AssignTable),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_RestoreCells, RestoreCells),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_SaveCells, SaveCells),
    OPENSC4_DETOUR(addr::cSC4NetworkWorldCache_CollectCells, CollectCells),
};

const Detour kCells[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkCellInfo_CombineEdges, CombineEdgesThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkCellInfo_Construct, ConstructCellInfoThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkCellInfo_CopyConstruct, CopyConstructCellInfo),
    OPENSC4_DETOUR(addr::cSC4NetworkCellInfo_Assign, AssignCellInfo),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MakeCellInfo, MakeCellInfo),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_ToolCellEntry, CellEntryThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CreateCellInfosForCurrentPlacementTiles,
                   CreateCellInfosForCurrentPlacementTiles),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MarkRuleCells, MarkRuleCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_NeighborIsClear, NeighborIsClear),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_NeighborEdges, NeighborEdges),
};

#undef OPENSC4_DETOUR

bool InstallCache(registry::Mode) { return InstallDetours(kCache, "cache"); }
bool InstallCells(registry::Mode) { return InstallDetours(kCells, "cells"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.cache", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkWorldCache_GetCell,
                        "Our network world cache (the tool's +0x1C): its constructor and destructor, Reset, a "
                        "table's clear, GetCell (which NAM calls), the node insert, find or insert (two forms), the "
                        "solved index reset, the table copy, saving and restoring the cells and collecting them "
                        "into a vector (13 detours; src/core/nettool_cache.cpp)",
                        false, InstallCache}),
    registry::Register({"simulators.nettool.cells", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_ToolCellEntry,
                        "Our network cell infos: their constructor, copy, assignment and edge sum, the tool's cell "
                        "entry (which NAM calls) and the maker from a cell's occupant, slot 71 "
                        "CreateCellInfosForCurrentPlacementTiles, the RUL0 rule's cell marking and the two slots of "
                        "the neighbour walker (10 detours; src/core/nettool_cache.cpp)",
                        false, InstallCells}),
};

} // namespace
} // namespace opensc4::nettool_sim
