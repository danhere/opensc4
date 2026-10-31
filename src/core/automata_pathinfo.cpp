// Our path information, parts pi1, pi2 and pi3a (build 641): cSC4PathInfo, the
// resource of type 0x296678F7 (interface 0xA9667793) that holds a network
// piece's paths (the SC4PATHS files), and cSC4PathInfoFactory, its resource
// factory (class ID 0x496678FE). pi1: the factory (its class ID, Create,
// deleting destructor and CreateInstance), the pool's free and new page, the
// object (constructor, deleting destructor, copy constructor, Clone,
// QueryInterface), the small queries (slots 3, 4, 10, 11, 13, 16, 17 and 36)
// and the building (AddPath, AddStop and their packed forms, the free path
// number, the points' reserve). pi2: the SC4PATHS reader (the factory's slot
// 10) with its line, number and point readers and the type 2 mirror; the path
// table's insert_unique, insert_equal, insert, operator[] and copy; slots 12,
// 14, 15, 27, 28, 32 to 35, 38 and 40 and the highest number. pi3a: the
// searches, slots 5 to 9 (the nearest path, the paths near a point by
// their first or last points, a random path two ways), 18 to 23 (a point
// ahead, a point projected on a path and on all paths, the farthest
// middle, a path's middle, a random point on a path), 31 (paths copied as
// another type), 37 (the key nearest the middle) and 39 (the crossing
// path). The survey is
// in docs/automata.md, "Part pi". Calls between functions of the part go to
// the game's addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.pathinfo: the 61 detours in kObject (pi1), kTables
//   (pi2) and kSearches (pi3a) below.
//
// What it does. A path information is a 0x3C-byte block of the pool
// 0x00B09FEC (4 KB pages of 0x44 blocks): +0 the interface (vtable
// 0x00A99E70), +4 the resource part (cRZResource, vtable 0x00A99E38: its
// reference count +8, its key +0xC, the type first), +0x1C the path table (a
// hash map of 100 buckets from the key to a record), +0x30 the flags (1 slot
// 16's and 17's, 2 there are stops), +0x31 a byte per path type (0 to 7) that
// says it has one. A key is type << 24 | number << 16 | from << 8 | to, the
// type's high nibble 1 or 2 for a stop. A node is 0x1C bytes: next, key, the
// record (+8: the points, a vector of 12-byte points; +0x14 the length;
// +0x18 a byte). The factory's CreateInstance makes one for the type
// 0x296678F7; its slot 10 reads an SC4PATHS file into it through
// AddPathByParts and AddStopByParts, then mirrors the type 2 paths.
//
// Kept as the game's: the adjustors 0x005403D0 (+4 slot 0) and 0x005403E0
// (+4 slot 12), AddRef and Release (0x00808600, 0x00808610, shared), the
// table's clear, resize, begin and next bucket (shared), the points'
// allocate-and-copy 0x0053D5A0 (also called from 0x0050C540), the vectors'
// and strings' code, the base classes' constructors and destructors, and
// the geometry and transforms of pi3b (slots 25, 26, 29 and their helpers
// 0x0053D160, 0x0053D280, 0x0053D600, 0x0053F940, 0x005403F0).
//
// Oddities kept (pi1): QueryInterface holds through the object's slot 1
// whatever it answers; 0x456B8F1D on a null object answers null and 0;
// HasPaths reads +0x31 + type with no bound (the type 0 is the stop flag 2);
// AddStop's value carries a length and a byte from the game's stack it never
// set (stored only when the insert fails to happen; ours 0); FreePathNumber
// compares the number signed (a "negative" one starts from 0 by its low
// byte) and stops at 26 with that key even when it is taken; the packed
// forms keep only the low byte of each part (the type's too, after the
// stop's 0x10 or 0x20 is ORed in). AL only in the game: CreateInstance,
// QueryInterface, HasPaths, AddStop; ours zero-extends.
//
// Oddities kept (pi2; docs/automata.md has them all): the line reader does
// not test the stream's answer (a failed read repeats the last byte) and
// after a CR reads and counts one more byte with no test of the count
// (from 1 it wraps and reads on); an empty line ends a read as a failure;
// the reader's failures keep what was added, a stop's answer is not tested,
// a path's key there already fails the whole file; slots 35, 32 and 40 read
// a record's first or last point unchecked; operator[]'s new record and
// slot 40's cell and point carry the game's stack words (ours 0); the copy
// does not free the table's old nodes; the highest number takes three
// arguments it never reads; the mirror walks the table as it grows, so
// mirrors are mirrored in turn up to the number 26. AL only in the game: the
// readers, slots 35 and 40; ours zero-extends.
//
// Oddities kept (pi3a): the floats are the game's x87 sums in 53 bits
// (doubles here), rounded where the game stores them; slots 5 to 9 and 37
// pack their key as (type << 16 | from) << 8 | to, so the type argument's
// low byte is the whole type byte (its higher bits shifted out) and the
// number is never given; slot 5's ties within 0.01 and slot
// 23's distance draw a new SC4Random seeded from the clock (-1; counted
// under the game's call site, fixed_seed.h); slot 18 reads the last point
// before it tests for none; slot 19 moves y by the squared distances'
// ratio; slot 22 does not stop at the middle (later segments from the same
// start overwrite it); slot 23 walks an empty record past its points (the
// count less 1, unsigned); slots 6, 7 and 39 read a record's first or
// last point unchecked; slot 21 takes stops too and starts from 0 (the
// farthest); slot 37 answers its own packed key when no path matches;
// slot 39 writes the index out as each nearer crossing is found; slot 31
// ORs the new type in under the key's high nibble (its bits past the byte
// shifted out) and walks the table as it grows (copies are copied in
// turn). AL only in the game: slots 18, 21, 22, 23 and 39; ours
// zero-extends.
//
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_pathinfo {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename T>
inline T Global(std::uintptr_t address)
{
    return *reinterpret_cast<const T*>(address);
}

// A virtual call through the object's first vtable, `byteOffset` in bytes.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("automata pathinfo: first %s", name);
}

constexpr std::uint32_t kClassId = 0x496678FEu, kResourceType = 0x296678F7u;
constexpr std::uint32_t kIidResource = 0x456B8F1Du, kIidPathInfo = 0xA9667793u;
constexpr std::size_t kFactorySize = 0x10;

// The object's fields.
constexpr std::size_t kPart4 = 0x04, kTable = 0x1C, kBuckets = 0x20, kBucketsEnd = 0x24, kCount = 0x2C,
                      kFlags = 0x30, kTypes = 0x31;
// The node: next +0, key +4, the record +8 (points +8..+0x10, length +0x14,
// byte +0x18).
constexpr std::size_t kNodeKey = 0x04, kNodeRecord = 0x08, kNodeLength = 0x14;
// The record (node + 8): points, length +0xC, byte +0x10.
constexpr std::size_t kRecordLength = 0x0C, kRecordByte = 0x10;
// The pool's current page (pool + 4): the free head +8, the count +0xC.
constexpr std::uint32_t kPageMask = 0xFFFFF000u, kReopenCount = 0x40000033u;
constexpr std::uint32_t kBlocksPerPage = 0x44, kBlockSize = 0x3C, kLastBlock = 0xFC4;

namespace slot {
constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kGetPath = 0x0C, kGetPathLength = 0x28,
                      kGetStopPoint = 0x8C;
constexpr std::size_t kPartDelete = 0x30; // the +4 part's slot 12
} // namespace slot

// The key packed as slots 4, 11 and the others do: type << 24 | number << 16
// | from << 8 | to, each the low byte.
std::uint32_t PackKey(std::uint32_t type, std::uint32_t from, std::uint32_t to, std::uint32_t number)
{
    return (type << 24) | ((number & 0xFFu) << 16) | ((from & 0xFFu) << 8) | (to & 0xFFu);
}

// The node of a key in the table, or null: the bucket key % (the buckets'
// size, shifted as a signed count).
char* FindNode(char* self, std::uint32_t key)
{
    char* const buckets = At<char*>(self, kBuckets);
    const auto count = static_cast<std::uint32_t>(static_cast<std::int32_t>(At<char*>(self, kBucketsEnd) - buckets) >> 2);
    char* node = At<char*>(buckets, (key % count) * 4);
    while (node && At<std::uint32_t>(node, kNodeKey) != key)
        node = At<char*>(node, 0);
    return node;
}

// A block of the path information pool, as the game's inlined allocation:
// the page's free head (SC4EdgePool::Grow when it has none), the head moved
// on and the count up, both through the pool's current page read again.
char* PoolTake()
{
    char* block = At<char*>(Global<char*>(addr::cSC4PathInfo_Pool + 4), 8);
    if (!block)
        block = CallThis<char*>(addr::SC4EdgePool_Grow, reinterpret_cast<void*>(addr::cSC4PathInfo_Pool));
    At<char*>(Global<char*>(addr::cSC4PathInfo_Pool + 4), 8) = At<char*>(block, 0);
    ++At<std::uint32_t>(Global<char*>(addr::cSC4PathInfo_Pool + 4), 0xC);
    return block;
}

// The value insert_unique takes: the key, the points, the length, the byte.
struct Value {
    std::uint32_t key;
    char* points[3];
    std::uint32_t length;
    std::uint8_t byte;
    std::uint8_t pad[3];
};
static_assert(sizeof(Value) == 0x18, "the game's value");

// insert_unique's answer: the node, the table, whether it was inserted.
struct Inserted {
    char* node;
    void* table;
    std::uint8_t inserted;
    std::uint8_t pad[3];
};

// The insert both AddPath and AddStop do: an empty record's value, the
// table resized for one more, insert_unique, the value's points freed.
Inserted InsertEmpty(char* self, std::uint32_t key, std::uint32_t length, std::uint8_t byte)
{
    char* empty[3] = {};
    Value value{};
    value.key = key;
    CallThis<void*>(addr::SC4Float3Vector_CopyConstruct, value.points, empty);
    value.length = length;
    value.byte = byte;
    CallThis<void>(addr::SC4Uint32HashSet_Resize, self + kTable, At<std::uint32_t>(self, kCount) + 1);
    Inserted result{};
    CallThis<void>(addr::cSC4PathInfo_InsertUnique, self + kTable, &result, &value);
    if (value.points[0])
        CallCdecl<void>(addr::SC4_Free, value.points[0]);
    return result;
}

// ====================================================================================
// The factory

// 0x0053CF90, cdecl, no arguments: the factory's class ID.
std::uint32_t __cdecl FactoryGetClassId()
{
    static bool logged;
    FirstCall("FactoryGetClassId", logged);
    return kClassId;
}

// 0x0053D130, cdecl: 0x10 bytes of SC4::operator_new, the factory base's
// constructor, the vtable; null when the allocation fails.
void* __cdecl FactoryCreate()
{
    static bool logged;
    FirstCall("FactoryCreate", logged);
    char* const self = CallCdecl<char*>(addr::SC4_operator_new, kFactorySize);
    if (!self)
        return nullptr;
    CallThis<void*>(addr::cGZDBSegmentPackedFile_F97564D, self);
    At<std::uintptr_t>(self, 0) = addr::cSC4PathInfoFactory_Vtable;
    return self;
}

// 0x0053D570, slot 7 (flags), ret 4: the vtable, the factory base's
// destructor, SC4::operator_delete with the flag 1.
void* __fastcall FactoryDeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("FactoryDeletingDestructor", logged);
    At<std::uintptr_t>(self, 0) = addr::cSC4PathInfoFactory_Vtable;
    CallThis<void>(addr::cRZResourceFactory_Destruct, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// 0x00542800, slot 9 (type, iid, out), ret 0xC: for the type 0x296678F7 a
// pool block constructed (at the game's address) and asked for the
// interface (its slot 0); one that does not answer is released through its
// +4 part's slot 12 with the flag 1. Otherwise *out 0 and 0.
std::uint32_t __fastcall FactoryCreateInstance(void*, void*, std::uint32_t type, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("FactoryCreateInstance", logged);
    if (type == kResourceType) {
        char* const self = CallThis<char*>(addr::cSC4PathInfo_Construct, PoolTake());
        if (Virtual<std::uint8_t>(self, slot::kQueryInterface, iid, out))
            return 1;
        Virtual<void*>(self + kPart4, slot::kPartDelete, 1u);
    }
    *out = nullptr;
    return 0;
}

// ====================================================================================
// The pool

// 0x0053DC10, on the pool (block), ret 4: nothing for null; the block on its
// page's free list (the page is the block's address & 0xFFFFF000), the page
// let go at count 0 (SC4Pool::ReleaseBlock) and reopened at 0x40000033
// (SC4Pool::ReopenBlock).
void __fastcall PoolFree(void* pool, void*, char* block)
{
    static bool logged;
    FirstCall("PoolFree", logged);
    if (!block)
        return;
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(block) & kPageMask);
    At<char*>(block, 0) = At<char*>(page, 8);
    At<char*>(page, 8) = block;
    const std::uint32_t count = --At<std::uint32_t>(page, 0xC);
    if (count == 0)
        CallThis<void>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (count == kReopenCount)
        CallThis<void>(addr::SC4Pool_ReopenBlock, pool, page);
}

// 0x0053D0C0, on the pool (not read), no arguments: a page of
// SC4::AllocatePoolPage(0xC) with its 0x44 blocks chained from +0x10 up to
// +0xFC4 (the last one's next null), the free head +8 the first, the count
// +0xC zero.
void* __fastcall PoolNewPage(void*, void*)
{
    static bool logged;
    FirstCall("PoolNewPage", logged);
    char* const page = CallCdecl<char*>(addr::SC4_AllocatePoolPage, 0xCu);
    char* next = nullptr;
    char* block = page + kLastBlock;
    for (std::uint32_t i = 0; i < kBlocksPerPage; ++i, block -= kBlockSize) {
        At<char*>(block, 0) = next;
        next = block;
    }
    At<char*>(page, 8) = next;
    At<std::uint32_t>(page, 0xC) = 0;
    return page;
}

// ====================================================================================
// The object

// The fields both constructors set before the table: the interface's
// vtable, the resource part, the class's two vtables, the table's buckets
// and count zero.
void ConstructStart(char* self)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4PathInfo_InterfaceVtable;
    CallThis<void>(addr::cGZDBSegmentPackedFile_F975633, self + kPart4);
    At<std::uintptr_t>(self, 0) = addr::cSC4PathInfo_Vtable;
    At<std::uintptr_t>(self, kPart4) = addr::cSC4PathInfo_Vtable4;
    for (std::size_t at = kBuckets; at <= kCount; at += 4)
        At<std::uint32_t>(self, at) = 0;
}

// 0x005407D0, no arguments: ConstructStart, the table's 100 buckets
// (0x00829BC0), the flags and the eight type bytes zero.
void* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Construct", logged);
    ConstructStart(self);
    CallThis<void>(addr::SC4Uint32HashSet_Construct, self + kTable, 100u);
    At<std::uint8_t>(self, kFlags) = 0;
    std::memset(self + kTypes, 0, 8);
    return self;
}

// 0x00540820, (flags) ret 4: the class's vtables, the table cleared
// (0x00584660) and its buckets freed, the resource part's destructor; with
// the flag 1 the block back to the pool as PoolFree does it.
void* __fastcall DeletingDestructor(char* self, void*, std::uint32_t flags)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    At<std::uintptr_t>(self, 0) = addr::cSC4PathInfo_Vtable;
    At<std::uintptr_t>(self, kPart4) = addr::cSC4PathInfo_Vtable4;
    CallThis<void>(addr::SC4PtrVectorHashMap_Clear, self + kTable);
    if (char* const buckets = At<char*>(self, kBuckets))
        CallCdecl<void>(addr::SC4_Free, buckets);
    CallThis<void>(addr::cRZResource_Destruct, self + kPart4);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & kPageMask);
        At<char*>(self, 0) = At<char*>(page, 8);
        const std::uint32_t count = At<std::uint32_t>(page, 0xC) - 1;
        At<char*>(page, 8) = self;
        At<std::uint32_t>(page, 0xC) = count;
        void* const pool = reinterpret_cast<void*>(addr::cSC4PathInfo_Pool);
        if (count == 0)
            CallThis<void>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (count == kReopenCount)
            CallThis<void>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// 0x005408B0, (from) ret 4: ConstructStart, the table copied from the
// other's (0x00540160), its flags and eight type bytes; the resource part's
// key is not copied.
void* __fastcall CopyConstruct(char* self, void*, char* from)
{
    static bool logged;
    FirstCall("CopyConstruct", logged);
    ConstructStart(self);
    CallThis<void>(addr::cSC4PathInfo_CopyPaths, self + kTable, from + kTable);
    At<std::uint8_t>(self, kFlags) = At<std::uint8_t>(from, kFlags);
    for (std::size_t i = 0; i < 8; ++i)
        At<std::uint8_t>(self, kTypes + i) = At<std::uint8_t>(from, kTypes + i);
    return self;
}

// 0x00540910, slot 24, no arguments: a pool block copy-constructed from
// this (at the game's address) and held through its slot 1.
void* __fastcall Clone(char* self, void*)
{
    static bool logged;
    FirstCall("Clone", logged);
    char* const copy = CallThis<char*>(addr::cSC4PathInfo_CopyConstruct, PoolTake(), self);
    Virtual<std::uint32_t>(copy, slot::kAddRef);
    return copy;
}

// 0x0053CFA0, slot 0 (iid, out), ret 8: the resource part (+4) for
// 0x456B8F1D (null for a null this), the object for 0xA9667793, else *out 0
// and 0; an answer is held through this object's slot 1.
std::uint32_t __fastcall QueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    *out = nullptr;
    if (iid == kIidResource)
        *out = self ? self + kPart4 : nullptr;
    else if (iid == kIidPathInfo)
        *out = self;
    else {
        *out = nullptr;
        return 0;
    }
    if (!*out)
        return 0;
    Virtual<std::uint32_t>(self, slot::kAddRef);
    return 1;
}

// ====================================================================================
// The queries

// 0x0053DD30, slot 3 (key, length out), ret 8: the key's record (node + 8)
// or null; with a record and an out pointer the length's word through it.
void* __fastcall GetPath(char* self, void*, std::uint32_t key, std::uint32_t* length)
{
    static bool logged;
    FirstCall("GetPath", logged);
    char* const node = FindNode(self, key);
    if (!node)
        return nullptr;
    if (length)
        *length = At<std::uint32_t>(node, kNodeLength);
    return node + kNodeRecord;
}

// 0x0053CFF0, slot 4 (type, from, to, number, length out), ret 0x14: slot 3
// with the packed key.
void* __fastcall GetPathByParts(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                                std::uint32_t number, void* length)
{
    static bool logged;
    FirstCall("GetPathByParts", logged);
    return Virtual<void*>(self, slot::kGetPath, PackKey(type, from, to, number), length);
}

// 0x0053DD90, slot 10 (key), ret 4: the key's length, else the float
// 0x00A81054; in ST0.
float __fastcall GetPathLength(char* self, void*, std::uint32_t key)
{
    static bool logged;
    FirstCall("GetPathLength", logged);
    char* const node = FindNode(self, key);
    return node ? At<float>(node, kNodeLength) : Global<float>(0x00A81054);
}

// 0x0053D030, slot 11 (type, from, to, number), ret 0x10: slot 10 with the
// packed key.
float __fastcall GetPathLengthByParts(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                                      std::uint32_t number)
{
    static bool logged;
    FirstCall("GetPathLengthByParts", logged);
    return Virtual<float>(self, slot::kGetPathLength, PackKey(type, from, to, number));
}

// 0x0053D0A0, slot 13 (type), ret 4: the byte +0x31 + type (no bound), or
// for 0 the flag 2 of +0x30.
std::uint32_t __fastcall HasPaths(char* self, void*, std::uint32_t type)
{
    static bool logged;
    FirstCall("HasPaths", logged);
    if (type)
        return At<std::uint8_t>(self, kTypes + type);
    return (At<std::uint8_t>(self, kFlags) >> 1) & 1u;
}

// 0x005403A0, slot 16, no arguments: the flag 1 of +0x30.
std::uint32_t __fastcall GetFlag(char* self, void*)
{
    static bool logged;
    FirstCall("GetFlag", logged);
    return At<std::uint8_t>(self, kFlags) & 1u;
}

// 0x005403B0, slot 17 (on), ret 4: the flag 1 of +0x30 by the argument's low
// byte; EAX the new flags byte, as the game leaves it.
std::uint32_t __fastcall SetFlag(char* self, void*, std::uint32_t on)
{
    static bool logged;
    FirstCall("SetFlag", logged);
    std::uint8_t flags = At<std::uint8_t>(self, kFlags);
    flags = (on & 0xFFu) ? static_cast<std::uint8_t>(flags | 1u) : static_cast<std::uint8_t>(flags & 0xFEu);
    At<std::uint8_t>(self, kFlags) = flags;
    return flags;
}

// 0x0053D060, slot 36 (point out, type, from, to, number), ret 0x14: slot 35
// with the key packed from the type | 0x10.
std::uint32_t __fastcall GetStopPointByParts(char* self, void*, void* point, std::uint32_t type, std::uint32_t from,
                                             std::uint32_t to, std::uint32_t number)
{
    static bool logged;
    FirstCall("GetStopPointByParts", logged);
    return Virtual<std::uint32_t>(self, slot::kGetStopPoint, point, PackKey(type | 0x10u, from, to, number));
}

// ====================================================================================
// The building

// 0x00540960, (key) ret 4: an empty record inserted for the key (the length
// and byte 0); the new record, or null when the key was there.
void* __fastcall AddPath(char* self, void*, std::uint32_t key)
{
    static bool logged;
    FirstCall("AddPath", logged);
    const Inserted result = InsertEmpty(self, key, 0, 0);
    if (!result.inserted || !result.node)
        return nullptr;
    return result.node + kNodeRecord;
}

// 0x00540A00, (point, key) ret 8: as AddPath (the value's length and byte
// are the game's stack leftovers, read only by the insert that does not
// happen; ours 0); a new record gets the length and byte 0, the one point
// (0x004FC6B0: one copy), its type's byte (+0x31 + (key >> 24 & 0xF)) 1 and
// the flag 2. The answer is whether it was inserted.
std::uint32_t __fastcall AddStop(char* self, void*, const float* point, std::uint32_t key)
{
    static bool logged;
    FirstCall("AddStop", logged);
    const Inserted result = InsertEmpty(self, key, 0, 0);
    if (!result.inserted)
        return 0;
    char* const record = result.node + kNodeRecord;
    At<std::uint32_t>(record, kRecordLength) = 0;
    At<std::uint8_t>(record, kRecordByte) = 0;
    CallThis<void>(addr::SC4Float3Vector_ResizeFill, record, 1u, point);
    At<std::uint8_t>(self, kTypes + ((key >> 24) & 0xFu)) = 1;
    At<std::uint8_t>(self, kFlags) |= 2;
    return 1;
}

// 0x00542880, (type, from, to, number) ret 0x10: AddPath (at the game's
// address) with the packed key.
void* __fastcall AddPathByParts(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                                std::uint32_t number)
{
    static bool logged;
    FirstCall("AddPathByParts", logged);
    return CallThis<void*>(addr::cSC4PathInfo_AddPath, self, PackKey(type, from, to, number));
}

// 0x005428B0, (point, type, from, to, number, second) ret 0x18: AddStop (at
// the game's address) with the key packed from ((second's low byte ? 2 : 1)
// << 4 | type).
std::uint32_t __fastcall AddStopByParts(char* self, void*, const float* point, std::uint32_t type,
                                        std::uint32_t from, std::uint32_t to, std::uint32_t number,
                                        std::uint32_t second)
{
    static bool logged;
    FirstCall("AddStopByParts", logged);
    const std::uint32_t kind = ((second & 0xFFu) ? 2u : 1u) << 4;
    return CallThis<std::uint32_t>(addr::cSC4PathInfo_AddStop, self, point, PackKey(kind | type, from, to, number));
}

// 0x0053DDE0, (type, from, to, number) ret 0x10: the packed key when it is
// not in the table; else the number (compared as a signed word, below 26)
// counted up, by its low byte, to the first key that is not; at 26 the key
// it has.
std::uint32_t __fastcall FreePathNumber(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                                        std::uint32_t number)
{
    static bool logged;
    FirstCall("FreePathNumber", logged);
    std::uint32_t key = PackKey(type, from, to, number);
    if (!FindNode(self, key))
        return key;
    auto n = static_cast<std::int32_t>(number);
    while (n < 0x1A) {
        ++n;
        key = PackKey(type, from, to, static_cast<std::uint32_t>(n));
        if (!FindNode(self, key))
            return key;
    }
    return key;
}

// 0x0053DC50, on a points vector (count), ret 4: nothing when the capacity
// (a signed count of 12-byte items) is at least the count; else new storage
// for the count, the items copied in (0x0053D5A0) and the old freed, or for
// an empty vector SC4::stl_allocate (none for 0), the size kept.
void __fastcall ReservePoints(char** vector, void*, std::uint32_t count)
{
    static bool logged;
    FirstCall("ReservePoints", logged);
    char* const begin = vector[0];
    const std::int32_t capacity = static_cast<std::int32_t>(vector[2] - begin) / 12;
    if (static_cast<std::uint32_t>(capacity) >= count)
        return;
    const std::int32_t size = static_cast<std::int32_t>(vector[1] - begin) / 12;
    char* storage;
    if (begin) {
        storage = reinterpret_cast<char*(__stdcall*)(std::uint32_t, char*, char*)>(addr::cSC4PathInfo_AllocateCopy)(
            count, begin, vector[1]);
        if (vector[0])
            CallCdecl<void>(addr::SC4_Free, vector[0]);
    } else {
        storage = count ? CallCdecl<char*>(addr::SC4_stl_allocate, count * 12) : nullptr;
    }
    vector[0] = storage;
    vector[1] = storage + size * 12;
    vector[2] = storage + count * 12;
}

// ====================================================================================
// Part pi2: the path table's code, the queries over all paths, the SC4PATHS
// reader

// The game's string (vtable 0x00A80810: a char vector with its terminator,
// and a reference count), 0x14 bytes, as the readers keep it on the stack.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t refs;
};
static_assert(sizeof(GameString) == 0x14, "the game's string");

// The hash table's iterator (SC4HashTable::Begin's): the node, the table.
struct Iterator {
    char* node;
    char* table;
};

// The answer of insert_equal: the node, the table.
struct Placed {
    char* node;
    void* table;
};

namespace slot {
constexpr std::size_t kStreamRead = 0x10, kStreamSize = 0xD0, kStringAppend = 0x44, kPartType = 0x14,
                      kSetFlag = 0x44, kAddPathConnection = 0xD8, kCellPart = 0xC0, kPathInfo = 0xC4, kCell = 0xEC,
                      kCellOf = 0x2C, kCellToPoint = 0x184, kAppPreferences = 0x98, kDrivesOnLeft = 0x64;
} // namespace slot

// The texts the reader and its line reader use (pointers to "SC4PATHS" and
// "--"), the float the reader's points start as, the floats it compares.
constexpr std::uintptr_t kHeaderText = 0x00B09FE0, kCommentText = 0x00B09FE4, kStartFloat = 0x00B228C4,
                         kVersionStops = 0x00A8FAF4, kVersionByte = 0x00A99F24, kStepLength = 0x00A99F20,
                         kEndSlack = 0x00A99E20, kHalfCell = 0x00A8E178;

// The table's bucket count as the game's code takes it: the buckets' size
// in bytes shifted as a signed count.
std::uint32_t BucketCount(char* table)
{
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(At<char*>(table, 8) - At<char*>(table, 4)) >> 2);
}

char*& Bucket(char* table, std::uint32_t index)
{
    return At<char*>(At<char*>(table, 4), index * 4);
}

// The node after `node` in the table, as the inlined iterator steps: its
// chain's next, else the first node of the next bucket that has one after
// its key's bucket (the bucket count read as it is now).
char* NextNode(char* table, char* node)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    std::uint32_t bucket = At<std::uint32_t>(node, kNodeKey) % BucketCount(table);
    char* found = nullptr;
    while (++bucket < BucketCount(table)) {
        found = Bucket(table, bucket);
        if (found)
            break;
    }
    return found;
}

// The first node of the object's table (SC4HashTable::Begin, the game's).
Iterator First(char* self)
{
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, self + kTable, &it);
    return it;
}

// A node of 0x1C bytes from SC4::stl_allocate with the next null and the
// key, the points (copied), the length and the byte of a value (key +0,
// points +4, length +0x10, byte +0x14) or of another node (+4, +8, +0x14,
// +0x18).
char* NewNode(std::uint32_t key, const void* points, std::uint32_t length, std::uint8_t byte)
{
    char* const node = CallCdecl<char*>(addr::SC4_stl_allocate, 0x1Cu);
    At<char*>(node, 0) = nullptr;
    At<std::uint32_t>(node, kNodeKey) = key;
    CallThis<void*>(addr::SC4Float3Vector_CopyConstruct, node + kNodeRecord, points);
    At<std::uint32_t>(node, kNodeLength) = length;
    At<std::uint8_t>(node, 0x18) = byte;
    return node;
}

char* NewNode(const Value* value)
{
    return NewNode(value->key, value->points, value->length, value->byte);
}

char* CopyNode(char* from)
{
    return NewNode(At<std::uint32_t>(from, kNodeKey), from + kNodeRecord, At<std::uint32_t>(from, kNodeLength),
                   At<std::uint8_t>(from, 0x18));
}

// push_back of a key onto a vector of uint32 (begin, end, capacity): in
// place when there is room (nothing written at a null end), else
// SC4PtrVector::InsertN.
void PushKey(char** vector, std::uint32_t key)
{
    if (vector[1] == vector[2]) {
        std::uint32_t value = key, tag = key;
        CallThis<void>(addr::SC4PtrVector_InsertN, vector, vector[1], &value, &tag, 1u, 1u);
        return;
    }
    if (vector[1])
        At<std::uint32_t>(vector[1], 0) = key;
    vector[1] += 4;
}

// A string as the readers make one on the stack: 8 bytes of
// SC4::stl_allocate, empty, the vtable, the count zero.
void MakeString(GameString& s)
{
    s.begin = s.end = s.capacity = nullptr;
    char* const body = CallCdecl<char*>(addr::SC4_stl_allocate, 8u);
    s.begin = s.end = body;
    s.capacity = body + 8;
    *body = 0;
    s.vtable = addr::SC4String_Vtable;
    s.refs = 0;
}

// The string's end as the readers inline it: the vtable, the buffer freed.
void DropString(GameString& s)
{
    s.vtable = addr::SC4String_Vtable;
    if (s.begin)
        CallCdecl<void>(addr::SC4_Free, s.begin);
}

const char* Text(GameString& s)
{
    return CallThis<const char*>(addr::cRZBaseString_Data, &s);
}

std::uint8_t ReadLineAt(void* self, void* stream, GameString* line, std::uint32_t* remaining)
{
    return static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4PathInfo_ReadLine, self, stream, line,
                                                              remaining));
}

std::uint8_t ReadNumberAt(void* self, void* stream, std::uint32_t* out, std::uint32_t* remaining)
{
    return static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4PathInfo_ReadNumber, self, stream, out,
                                                              remaining));
}

std::uint8_t ReadPointAt(void* self, void* stream, float* point, std::uint32_t* remaining)
{
    return static_cast<std::uint8_t>(CallThis<std::uint32_t>(addr::cSC4PathInfo_ReadPoint, self, stream, point,
                                                              remaining));
}

// ---- The path table's code (on the table, the object + 0x1C: the buckets
// +4 to +8, the count +0x10)

// 0x0053FF10, (result, value) ret 8: insert_unique without a resize: the
// key's node when there is one (inserted 0), else a new node at the head of
// its bucket, the count up (inserted 1). The result's byte only; EAX the
// result.
Inserted* __fastcall InsertUnique(char* table, void*, Inserted* result, const Value* value)
{
    static bool logged;
    FirstCall("InsertUnique", logged);
    const std::uint32_t bucket = value->key % BucketCount(table);
    char* const head = Bucket(table, bucket);
    for (char* node = head; node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, kNodeKey) == value->key) {
            result->table = table;
            result->node = node;
            result->inserted = 0;
            return result;
        }
    char* const node = NewNode(value);
    At<char*>(node, 0) = head;
    Bucket(table, bucket) = node;
    ++At<std::uint32_t>(table, 0x10);
    result->node = node;
    result->table = table;
    result->inserted = 1;
    return result;
}

// 0x0053FFD0, (result, value) ret 8: insert_equal without a resize: a new
// node after the first of the key in its bucket, else at the bucket's head;
// the count up; the result {node, table}, EAX the result.
Placed* __fastcall InsertEqual(char* table, void*, Placed* result, const Value* value)
{
    static bool logged;
    FirstCall("InsertEqual", logged);
    const std::uint32_t bucket = value->key % BucketCount(table);
    char* const head = Bucket(table, bucket);
    for (char* same = head; same; same = At<char*>(same, 0))
        if (At<std::uint32_t>(same, kNodeKey) == value->key) {
            char* const node = NewNode(value);
            At<char*>(node, 0) = At<char*>(same, 0);
            At<char*>(same, 0) = node;
            ++At<std::uint32_t>(table, 0x10);
            result->table = table;
            result->node = node;
            return result;
        }
    char* const node = NewNode(value);
    At<char*>(node, 0) = head;
    Bucket(table, bucket) = node;
    ++At<std::uint32_t>(table, 0x10);
    result->node = node;
    result->table = table;
    return result;
}

// 0x005400D0, (value) ret 4: the table resized for one more (0x004F0940),
// a new node at the head of the key's bucket with no test for the key, the
// count up; answers the node's pair (node + 4).
char* __fastcall InsertNew(char* table, void*, const Value* value)
{
    static bool logged;
    FirstCall("InsertNew", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Resize, table, At<std::uint32_t>(table, 0x10) + 1);
    const std::uint32_t bucket = value->key % BucketCount(table);
    char* const head = Bucket(table, bucket);
    char* const node = NewNode(value);
    At<char*>(node, 0) = head;
    Bucket(table, bucket) = node;
    At<std::uint32_t>(table, 0x10) = At<std::uint32_t>(table, 0x10) + 1;
    return node + kNodeKey;
}

// 0x00540160, (from) ret 4: the copy of another table: the buckets emptied
// (the old nodes not freed), reserved (0x0045D4F0) and filled with nulls
// (0x004FBA20) to the other's bucket count, each chain copied in its order
// into new nodes, the count copied.
void __fastcall CopyPaths(char* table, void*, char* from)
{
    static bool logged;
    FirstCall("CopyPaths", logged);
    At<char*>(table, 8) = At<char*>(table, 4);
    CallThis<void>(addr::PtrVector_Reserve, table + 4, BucketCount(from));
    void* empty = nullptr;
    CallThis<void>(addr::SC4PtrVector_FillInsert, table + 4, At<char*>(table, 8), BucketCount(from), &empty);
    for (std::uint32_t i = 0; i < BucketCount(from); ++i) {
        char* source = Bucket(from, i);
        if (!source)
            continue;
        char* last = CopyNode(source);
        Bucket(table, i) = last;
        for (source = At<char*>(source, 0); source; source = At<char*>(source, 0)) {
            char* const node = CopyNode(source);
            At<char*>(last, 0) = node;
            last = node;
        }
    }
    At<std::uint32_t>(table, 0x10) = At<std::uint32_t>(from, 0x10);
}

// 0x005402D0, (key*) ret 4: operator[]: the key's record (node + 8), else
// one inserted by 0x005400D0 with no points; the game's record carries a
// length and a byte from its stack it never set (ours 0).
char* __fastcall FindOrInsert(char* table, void*, const std::uint32_t* key)
{
    static bool logged;
    FirstCall("FindOrInsert", logged);
    const std::uint32_t wanted = *key;
    for (char* node = Bucket(table, wanted % BucketCount(table)); node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, kNodeKey) == wanted)
            return node + kNodeRecord;
    char* empty[3] = {};
    Value value{};
    value.key = wanted;
    CallThis<void*>(addr::SC4Float3Vector_CopyConstruct, value.points, empty);
    char* const pair = CallThis<char*>(addr::cSC4PathInfo_InsertNew, table, &value);
    if (value.points[0])
        CallCdecl<void>(addr::SC4_Free, value.points[0]);
    return pair + 4;
}

// ---- The queries over all paths (slots, ret as given)

// 0x0053EAA0, slot 12 (type, from, to), ret 0xC: the count of keys with the
// type byte, from and to (any number).
std::uint32_t __fastcall CountPaths(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to)
{
    static bool logged;
    FirstCall("CountPaths", logged);
    const std::uint32_t wanted = (type << 24) | ((from & 0xFFu) << 8) | (to & 0xFFu);
    std::uint32_t count = 0;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node))
        if ((At<std::uint32_t>(node, kNodeKey) & 0xFF00FFFFu) == wanted)
            ++count;
    return count;
}

// The keys slots 14 and 15 put in the vector: paths (type high nibble 0) of
// the type nibble with the side byte (from: >> 8, to: >> 0) given.
std::uint32_t KeysOfSide(char* self, std::uint32_t type, std::uint8_t side, char** vector, unsigned shift)
{
    vector[1] = vector[0];
    std::uint32_t count = 0;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if ((key & 0xF0000000u) || ((key >> 24) & 0xFu) != type || static_cast<std::uint8_t>(key >> shift) != side)
            continue;
        PushKey(vector, key);
        ++count;
    }
    return count;
}

// 0x0053E870, slot 14 (type, from, vector), ret 0xC: the vector emptied,
// the keys of paths of the type from the side put in; the count.
std::uint32_t __fastcall KeysFrom(char* self, void*, std::uint32_t type, std::uint32_t from, char** vector)
{
    static bool logged;
    FirstCall("KeysFrom", logged);
    return KeysOfSide(self, type, static_cast<std::uint8_t>(from), vector, 8);
}

// 0x0053E990, slot 15 (type, to, vector), ret 0xC: as slot 14 by the side
// the path goes to.
std::uint32_t __fastcall KeysTo(char* self, void*, std::uint32_t type, std::uint32_t to, char** vector)
{
    static bool logged;
    FirstCall("KeysTo", logged);
    return KeysOfSide(self, type, static_cast<std::uint8_t>(to), vector, 0);
}

// 0x0053EB30, (three arguments, not read) ret 0xC: the highest number byte
// over every key, stops too; 0 for none.
std::uint32_t __fastcall HighestNumber(char* self, void*, std::uint32_t, std::uint32_t, std::uint32_t)
{
    static bool logged;
    FirstCall("HighestNumber", logged);
    std::int32_t highest = 0;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const auto number = static_cast<std::int32_t>((At<std::uint32_t>(node, kNodeKey) >> 16) & 0xFFu);
        if (highest < number)
            highest = number;
    }
    return static_cast<std::uint32_t>(highest);
}

// 0x0053EBB0, slot 27 (occupant), ret 4: each path (type high nibble 0) to
// the occupant's slot 54 (AddPathConnection) as (the type nibble, key >> 8,
// key).
void __fastcall ConnectPaths(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("ConnectPaths", logged);
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (!(key & 0xF0000000u))
            Virtual<void>(occupant, slot::kAddPathConnection, (key >> 24) & 0xFu, key >> 8, key);
    }
}

// 0x0053EC40, slot 28 (x, y, z), ret 0xC: every point of every record moved
// by the three floats.
void __fastcall MovePaths(char* self, void*, float x, float y, float z)
{
    static bool logged;
    FirstCall("MovePaths", logged);
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node))
        for (float* p = At<float*>(node, kNodeRecord); p != At<float*>(node, kNodeRecord + 4); p += 3) {
            p[0] = x + p[0];
            p[1] = y + p[1];
            p[2] = z + p[2];
        }
}

// Whether a key matches slot 33's and 34's wanted key with the key's from
// and to 0xFF as wild cards.
std::uint32_t WildSides(std::uint32_t key)
{
    std::uint32_t mask = static_cast<std::uint8_t>(key >> 8) == 0xFF ? 0xFF00u : 0u;
    if (static_cast<std::uint8_t>(key) == 0xFF)
        mask |= 0xFFu;
    return mask;
}

// 0x0053E650, slot 33 (key), ret 4: the count of keys equal to the key with
// the type's high nibble 1 (its number 0xFF a wild card), or equal to it
// with their own from and to 0xFF as wild cards.
std::uint32_t __fastcall CountStopsOfKey(char* self, void*, std::uint32_t key)
{
    static bool logged;
    FirstCall("CountStopsOfKey", logged);
    const std::uint32_t number = static_cast<std::uint8_t>(key >> 16) == 0xFF ? 0xFF0000u : 0u;
    const std::uint32_t wanted = (key & 0x0FFFFFFFu) | 0x10000000u;
    std::uint32_t count = 0;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t k = At<std::uint32_t>(node, kNodeKey);
        const std::uint32_t sides = WildSides(k);
        if ((k | number) == (wanted | number) || (sides | k) == (sides | wanted))
            ++count;
    }
    return count;
}

// 0x0053E550, slot 34 (type, from, to, number), ret 0x10: the count of
// first stops (type | 0x10) of the parts, wild cards: the type 0 (its
// nibble), 0xFF from, to and number, the keys' own from and to 0xFF.
std::uint32_t __fastcall CountStops(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                                    std::uint32_t number)
{
    static bool logged;
    FirstCall("CountStops", logged);
    std::uint32_t wild = type == 0 ? 0x0F000000u : 0u;
    if ((from & 0xFFu) == 0xFF)
        wild |= 0xFF00u;
    if ((to & 0xFFu) == 0xFF)
        wild |= 0xFFu;
    if ((number & 0xFFu) == 0xFF)
        wild |= 0xFF0000u;
    const std::uint32_t wanted = ((((type | 0x10u) << 8 | (number & 0xFFu)) << 8 | (from & 0xFFu)) << 8) | (to & 0xFFu);
    std::uint32_t count = 0;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t k = At<std::uint32_t>(node, kNodeKey);
        const std::uint32_t sides = WildSides(k);
        if ((sides | k | wild) == (sides | wanted | wild))
            ++count;
    }
    return count;
}

// 0x0053E740, slot 35 (point out, key), ret 8: the first point of the key's
// record; else of the first record (in the table's order, the iterator's
// step 0x0053DCF0) whose key matches the key with the type's high nibble 1,
// 0xFF from and to on either side wild cards. AL 1, or 0 (EAX 0) with none.
// The point is read unchecked.
std::uint32_t __fastcall GetStopPoint(char* self, void*, std::uint32_t* point, std::uint32_t key)
{
    static bool logged;
    FirstCall("GetStopPoint", logged);
    char* const table = self + kTable;
    for (char* node = Bucket(table, key % BucketCount(table)); node; node = At<char*>(node, 0))
        if (At<std::uint32_t>(node, kNodeKey) == key) {
            const std::uint32_t* const p = At<std::uint32_t*>(node, kNodeRecord);
            point[0] = p[0];
            point[1] = p[1];
            point[2] = p[2];
            return 1;
        }
    const std::uint32_t wanted = (key & 0x0FFFFFFFu) | 0x10000000u;
    Iterator it = First(self);
    while (it.node) {
        const std::uint32_t k = At<std::uint32_t>(it.node, kNodeKey);
        std::uint32_t mask = 0xFFFFFFFFu;
        if (static_cast<std::uint8_t>(k >> 8) == 0xFF)
            mask = 0xFFFF00FFu;
        if (static_cast<std::uint8_t>(k) == 0xFF)
            mask &= 0xFFFFFF00u;
        if (static_cast<std::uint8_t>(key >> 8) == 0xFF)
            mask &= 0xFFFF00FFu;
        if (static_cast<std::uint8_t>(key) == 0xFF)
            mask &= 0xFFFFFF00u;
        if ((mask & k) == (mask & wanted)) {
            const std::uint32_t* const p = At<std::uint32_t*>(it.node, kNodeRecord);
            point[0] = p[0];
            point[1] = p[1];
            point[2] = p[2];
            return 1;
        }
        char* next = At<char*>(it.node, 0);
        if (!next)
            next = CallThis<char*>(addr::SC4HashTable_NextBucket, &it);
        it.node = next;
    }
    return 0;
}

// 0x0053F000, slot 38 (type, from, vector), ret 0xC: the vector emptied,
// the keys of the type nibble (stops too) from the side whose record's byte
// has the bit 1 put in; EAX whether the vector has any.
std::uint32_t __fastcall FlaggedKeys(char* self, void*, std::uint32_t type, std::uint32_t from, char** vector)
{
    static bool logged;
    FirstCall("FlaggedKeys", logged);
    vector[1] = vector[0];
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (static_cast<std::uint8_t>(key >> 8) == static_cast<std::uint8_t>(from) && ((key >> 24) & 0xFu) == type &&
            (At<std::uint8_t>(node, 0x18) & 1u))
            PushKey(vector, key);
    }
    return vector[0] != vector[1] ? 1u : 0u;
}

// Whether a record's end (x +0, z +8) is off the cell's side `side` (0 low
// x, 1 low z, 2 high x, 3 high z; the bounds floats): its distance not below
// the slack 0x00A99E20 (1; unordered too). False for another side.
bool OffSide(std::uint32_t side, const float* end, const float* bounds)
{
    double distance;
    switch (side) {
    case 0: distance = static_cast<double>(end[0]) - bounds[0]; break;
    case 1: distance = static_cast<double>(end[2]) - bounds[1]; break;
    case 2: distance = static_cast<double>(end[0]) - bounds[2]; break;
    case 3: distance = static_cast<double>(end[2]) - bounds[3]; break;
    default: return false;
    }
    return !(std::fabs(distance) < static_cast<double>(Global<float>(kEndSlack)));
}

// 0x0053F6C0, slot 40 (occupant), ret 4: the occupant's cell (its slot 59)
// to a point (the city's slot 97), the cell's sides the point's x and z
// less and plus 8 (floats); each path's (type high nibble 0) first point
// against its from side and last point against its to side: one off by 1
// or more marks the record's byte with 2 and the answer 0. AL 1 when none.
// The points are read unchecked.
std::uint32_t __fastcall CheckEnds(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("CheckEnds", logged);
    // The cell and the point: words the slots may leave unset (the game's x
    // is the occupant argument's stack word, the others stack leftovers; ours
    // zero).
    std::uint32_t cellX = 0, cellZ = 0;
    Virtual<void>(occupant, slot::kCell, &cellX, &cellZ);
    float x = 0.0f, z = 0.0f;
    Virtual<void>(Global<void*>(addr::SC4_City), slot::kCellToPoint, cellX, cellZ, &x, &z);
    const float half = Global<float>(kHalfCell);
    const float bounds[4] = {x - half, z - half, x + half, z + half};
    std::uint32_t answer = 1;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (key & 0xF0000000u)
            continue;
        // Both ends read before the sides are looked at, as the game does.
        const float* const begin = At<float*>(node, kNodeRecord);
        const float* const end = At<float*>(node, kNodeRecord + 4) - 3;
        const float first[3] = {begin[0], 0.0f, begin[2]};
        const float last[3] = {end[0], 0.0f, end[2]};
        if (OffSide((key >> 8) & 0xFFu, first, bounds)) {
            At<std::uint8_t>(node, 0x18) |= 2;
            answer = 0;
        }
        if (OffSide(key & 0xFFu, last, bounds)) {
            At<std::uint8_t>(node, 0x18) |= 2;
            answer = 0;
        }
    }
    return answer;
}

// 0x0053FD70, slot 32 (occupant, other), ret 8: the other's side from the
// two cells (their slot 48's slot 11: x first, then z, compared signed: 2
// when the other's x is greater, 3 its z, 0 its x less, 1 its z less, else
// 0); with the other's path information (its slot 49) each path to that
// side whose key the other has with points: the other's first point
// appended to this path when this path's last point is farther from it
// than 1. This path's points are read unchecked.
void __fastcall JoinEnds(char* self, void*, void* occupant, void* other)
{
    static bool logged;
    FirstCall("JoinEnds", logged);
    std::int32_t mine[4] = {}, theirs[4] = {};
    Virtual<void>(Virtual<void*>(occupant, slot::kCellPart), slot::kCellOf, mine);
    Virtual<void>(Virtual<void*>(other, slot::kCellPart), slot::kCellOf, theirs);
    std::uint8_t side = 0;
    if (mine[0] < theirs[0])
        side = 2;
    else if (mine[1] < theirs[1])
        side = 3;
    else if (mine[0] > theirs[0])
        side = 0;
    else if (mine[1] > theirs[1])
        side = 1;
    void* const info = Virtual<void*>(other, slot::kPathInfo);
    if (!info)
        return;
    char* const table = self + kTable;
    for (char* node = First(self).node; node; node = NextNode(table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (static_cast<std::uint8_t>(key) != side)
            continue;
        char** const record = Virtual<char**>(info, slot::kGetPath, key, static_cast<void*>(nullptr));
        if (!record || record[0] == record[1])
            continue;
        float start[3];
        std::memcpy(start, record[0], sizeof start);
        const float* const last = At<float*>(node, kNodeRecord + 4) - 3;
        const float dx = static_cast<float>(static_cast<double>(last[0]) - start[0]);
        const double dy = static_cast<double>(last[1]) - start[1];
        const double dz = static_cast<double>(last[2]) - start[2];
        const double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
        if (distance > static_cast<double>(Global<float>(kEndSlack)))
            CallThis<void>(addr::SC4KeyVector_PushBack, node + kNodeRecord, start);
    }
}

// ---- The SC4PATHS reader

// 0x0053DEB0, (stream, string, remaining) ret 0xC, this only passed on: the
// string emptied; bytes read one at a time (the stream's slot 4, its answer
// not tested; a failed read leaves the last byte) while the remaining count
// is not 0, each counted down, to a line end: '\n', or '\r' and the byte
// after it (read and counted with no test of the count), the others
// appended (the string's slot 17). A line starting with "--" is skipped by
// reading the next (a call to itself). AL whether the line is not empty.
std::uint32_t __fastcall ReadLine(void* self, void*, void* stream, GameString* line, std::uint32_t* remaining)
{
    static bool logged;
    FirstCall("ReadLine", logged);
    if (line->begin != line->end) {
        *line->begin = 0;
        line->end = line->begin;
    }
    // The game reads into its string argument's stack word.
    std::uint32_t word = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(line));
    while (*remaining != 0) {
        Virtual<std::uint32_t>(stream, slot::kStreamRead, &word, 1u);
        --*remaining;
        const char c = static_cast<char>(word);
        if (c == '\r') {
            Virtual<std::uint32_t>(stream, slot::kStreamRead, &word, 1u);
            --*remaining;
            break;
        }
        if (c == '\n')
            break;
        char byte = c;
        Virtual<void>(line, slot::kStringAppend, &byte, 1u);
    }
    if (line->begin != line->end &&
        (CallThis<std::uint32_t>(addr::cRZBaseString_BeginsWith, line, Global<const char*>(kCommentText), 2u) & 0xFFu))
        return CallThis<std::uint32_t>(addr::cSC4PathInfo_ReadLine, self, stream, line, remaining);
    return line->begin != line->end ? 1u : 0u;
}

// 0x0053F8A0, (stream, out, remaining) ret 0xC: *out 0, a line (0x0053DEB0)
// into a string of its own, *out its number (SC4::TextToUint32) when there
// is one; AL the line's answer.
std::uint32_t __fastcall ReadNumber(void* self, void*, void* stream, std::uint32_t* out, std::uint32_t* remaining)
{
    static bool logged;
    FirstCall("ReadNumber", logged);
    GameString line;
    MakeString(line);
    *out = 0;
    const std::uint8_t ok = ReadLineAt(self, stream, &line, remaining);
    if (ok)
        *out = CallCdecl<std::uint32_t>(addr::SC4_TextToUint32, Text(line));
    DropString(line);
    return ok;
}

// The line's text up to its first comma taken off it (SC4String::SplitOff)
// into the field (assigned unless the same), read as a double.
double NextField(GameString& line, GameString& field, std::uint8_t* tag)
{
    GameString piece;
    char* const made = CallThis<char*>(addr::SC4String_SplitOff, &line, &piece,
                                       reinterpret_cast<const char*>(addr::SC4_CommaText));
    char** const body = made ? reinterpret_cast<char**>(made + 4) : nullptr;
    if (body != &field.begin)
        CallThis<void>(addr::SC4String_AssignRange, &field.begin, body[0], body[1], tag);
    piece.vtable = addr::SC4String_Vtable;
    if (piece.begin)
        CallCdecl<void>(addr::SC4_Free, piece.begin);
    return CallCdecl<double>(addr::SC4_ParseDouble910991, Text(field));
}

// 0x00540610, (stream, point, remaining) ret 0xC: the point zero; a line
// (0x0053DEB0) as "x,z,y": x the first field, z the second negated, y the
// rest (SC4::ParseDouble910991 each, rounded to floats). AL the line's
// answer.
std::uint32_t __fastcall ReadPoint(void* self, void*, void* stream, float* point, std::uint32_t* remaining)
{
    static bool logged;
    FirstCall("ReadPoint", logged);
    GameString line, field;
    MakeString(line);
    MakeString(field);
    std::memset(point, 0, 12);
    std::uint8_t ok = ReadLineAt(self, stream, &line, remaining);
    if (ok) {
        point[0] = static_cast<float>(NextField(line, field, &ok));
        point[2] = static_cast<float>(-NextField(line, field, &ok));
        point[1] = static_cast<float>(CallCdecl<double>(addr::SC4_ParseDouble910991, Text(line)));
    }
    DropString(field);
    DropString(line);
    return ok;
}

// 0x005423D0, no arguments: for each path of the type nibble 2 (high
// nibble 0), in the table's order as it grows: its mirror, the key with
// from and to swapped, from the same number up (to 26; one that has all
// taken is passed by) to the first not in the table, added (AddPath at the
// game's address) with the path's points reversed (reserved first), its
// length and byte. Mirrors added are met by the walk too.
void __fastcall MirrorType2(char* self, void*)
{
    static bool logged;
    FirstCall("MirrorType2", logged);
    Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if ((key & 0xF0000000u) || ((key >> 24) & 0xFu) != 2)
            continue;
        const std::uint32_t to = key & 0xFFu, from = (key >> 8) & 0xFFu;
        std::uint8_t number = static_cast<std::uint8_t>(key >> 16);
        std::uint32_t mirror = 0x02000000u | (static_cast<std::uint32_t>(number) << 16) | (to << 8) | from;
        bool full = false;
        while (FindNode(self, mirror)) {
            if (number >= 0x1A) {
                full = true;
                break;
            }
            ++number;
            mirror = 0x02000000u | (static_cast<std::uint32_t>(number) << 16) | (to << 8) | from;
        }
        if (full)
            continue;
        char* const record = CallThis<char*>(addr::cSC4PathInfo_AddPath, self, mirror);
        if (!record)
            continue;
        const auto bytes = static_cast<std::int32_t>(At<char*>(node, kNodeRecord + 4) - At<char*>(node, kNodeRecord));
        CallThis<void>(addr::cSC4PathInfo_ReservePoints, record, static_cast<std::uint32_t>(bytes / 12));
        for (char* p = At<char*>(node, kNodeRecord + 4); p != At<char*>(node, kNodeRecord); p -= 12) {
            char* const where = At<char*>(record, 4);
            if (where == At<char*>(record, 8)) {
                std::uint8_t tag = 0;
                CallThis<void>(addr::SC4Float3Vector_Insert, record, where, p - 12, &tag, 1u, 1u);
            } else {
                if (where)
                    std::memcpy(where, p - 12, 12);
                At<char*>(record, 4) = where + 12;
            }
        }
        At<std::uint32_t>(record, kRecordLength) = At<std::uint32_t>(node, kNodeLength);
        At<std::uint8_t>(record, kRecordByte) = At<std::uint8_t>(node, 0x18);
    }
}

// 0x00542900, the factory's slot 10 (resource part, stream), ret 8: for a
// part (the object + 4) of the type 0x296678F7 (its slot 5) an SC4PATHS
// file of lines (the stream's slot 52 the byte count): "SC4PATHS"
// (cIGZString's inequality 0x00454BB0), the version (a float), the path
// count (up to 1000), from 1.1 the stop count (up to 1000), the flag
// (slot 17); per path the type (1 to 7), number, from and to (0 to 3 or
// 0xFF), from 1.2 its byte, the point count (1 up) and the points: the
// path added by parts (a key there already fails), its points reserved, the
// first point kept, each later one only farther than 1e-5 from the last
// read (the distance added to the length, as a float), the byte set per
// point; per stop the kind (1 or 2), type, number, from, to and a point,
// added by parts (its answer not tested) as the second when the kind is 1
// on a game that drives on the left (the app's slot 38's slot 25, 1 when
// there is none) or 2 on one that does not; then the type 2 paths
// mirrored. AL 1, or 0 at the first bad line (what was added stays).
std::uint32_t __fastcall ReadPaths(void* self, void*, char* part, void* stream)
{
    static bool logged;
    FirstCall("ReadPaths", logged);
    if (Virtual<std::uint32_t>(part, slot::kPartType) != kResourceType)
        return 0;
    char* const info = part - kPart4;
    GameString line;
    MakeString(line);
    std::uint32_t remaining = Virtual<std::uint32_t>(stream, slot::kStreamSize);
    std::uint32_t paths = 0, stops = 0, flag = 0;
    const std::uint32_t start = Global<std::uint32_t>(kStartFloat);
    float point[3], last[3];
    std::memcpy(&point[0], &start, 4);
    std::memcpy(&point[1], &start, 4);
    std::memcpy(&point[2], &start, 4);
    std::memcpy(last, point, sizeof last);
    std::uint8_t left = 1;
    void* const app = CallCdecl<void*>(addr::SC4_CurrentApp);
    if (void* const preferences = Virtual<void*>(app, slot::kAppPreferences, 0u))
        left = (Virtual<std::uint32_t>(preferences, slot::kDrivesOnLeft) & 0xFFu) == 1 ? 1 : 0;

    bool bad = true;
    if (ReadLineAt(self, stream, &line, &remaining)) {
        GameString header;
        CallThis<void*>(addr::SC4String_FromText, &header, Global<const char*>(kHeaderText));
        bad = (CallCdecl<std::uint32_t>(addr::SC4UI_F454BB0, &line, &header) & 0xFFu) != 0;
        DropString(header);
    }
    if (bad || !ReadLineAt(self, stream, &line, &remaining)) {
        DropString(line);
        return 0;
    }
    const auto version = static_cast<float>(CallCdecl<double>(addr::SC4_ParseDouble910991, Text(line)));
    const auto fail = [&line]() -> std::uint32_t {
        DropString(line);
        return 0;
    };
    if (!ReadNumberAt(self, stream, &paths, &remaining) || paths > 1000)
        return fail();
    if (version >= Global<float>(kVersionStops) && (!ReadNumberAt(self, stream, &stops, &remaining) || stops > 1000))
        return fail();
    if (!ReadNumberAt(self, stream, &flag, &remaining))
        return fail();
    Virtual<void>(info, slot::kSetFlag, (flag & 0xFFFFFF00u) | (flag != 0 ? 1u : 0u));

    const auto side = [](std::uint32_t v) { return v <= 3 || v == 0xFF; };
    for (std::uint32_t i = 0; i < paths; ++i) {
        std::uint32_t type, number, from, to, byte = 0, count;
        if (!ReadNumberAt(self, stream, &type, &remaining) || type == 0 || type >= 8)
            return fail();
        if (!ReadNumberAt(self, stream, &number, &remaining))
            return fail();
        if (!ReadNumberAt(self, stream, &from, &remaining) || !side(from))
            return fail();
        if (!ReadNumberAt(self, stream, &to, &remaining) || !side(to))
            return fail();
        if (version >= Global<float>(kVersionByte) && !ReadNumberAt(self, stream, &byte, &remaining))
            return fail();
        if (!ReadNumberAt(self, stream, &count, &remaining) || count == 0)
            return fail();
        char* const record = CallThis<char*>(addr::cSC4PathInfo_AddPathByParts, info, type, from, to, number);
        if (!record)
            return fail();
        CallThis<void>(addr::cSC4PathInfo_ReservePoints, record, count);
        for (std::uint32_t j = 0; j < count; ++j) {
            if (!ReadPointAt(self, stream, point, &remaining))
                return fail();
            if (j == 0) {
                CallThis<void>(addr::SC4KeyVector_PushBack, record, point);
            } else {
                const double dx = static_cast<double>(point[0]) - last[0];
                const double dy = static_cast<double>(point[1]) - last[1];
                const double dz = static_cast<double>(point[2]) - last[2];
                const double step = std::sqrt((dz * dz + dy * dy) + dx * dx);
                if (step > static_cast<double>(Global<float>(kStepLength))) {
                    CallThis<void>(addr::SC4KeyVector_PushBack, record, point);
                    At<float>(record, kRecordLength) = static_cast<float>(step) + At<float>(record, kRecordLength);
                }
            }
            At<std::uint8_t>(record, kRecordByte) = static_cast<std::uint8_t>(byte);
            std::memcpy(last, point, sizeof last);
        }
    }
    for (std::uint32_t i = 0; i < stops; ++i) {
        std::uint32_t kind = 1, type, number, from, to;
        if (!ReadNumberAt(self, stream, &kind, &remaining) || (kind != 1 && kind != 2))
            return fail();
        if (!ReadNumberAt(self, stream, &type, &remaining) || type == 0 || type >= 8)
            return fail();
        if (!ReadNumberAt(self, stream, &number, &remaining))
            return fail();
        if (!ReadNumberAt(self, stream, &from, &remaining) || !side(from))
            return fail();
        if (!ReadNumberAt(self, stream, &to, &remaining) || !side(to))
            return fail();
        if (!ReadPointAt(self, stream, point, &remaining))
            return fail();
        const std::uint32_t second = ((kind != 2) != (left != 0)) ? 1u : 0u;
        CallThis<std::uint32_t>(addr::cSC4PathInfo_AddStopByParts, info, point, type, from, to, number, second);
    }
    CallThis<void>(addr::cSC4PathInfo_MirrorType2, info);
    DropString(line);
    return 1;
}

// ====================================================================================
// Part pi3a: the searches (slots 5 to 9, 18 to 23, 31, 37 and 39)

namespace slot {
constexpr std::size_t kGetFlag = 0x40, kProjectOnPath = 0x4C, kMiddlePoint = 0x58;
} // namespace slot

// The floats the searches compare and scale by: 0, the tie's slack 0.01, a
// half, 3, 16, the dot slot 18 wants above (-0.2), slot 19's ratio floor
// (1e-4), slot 20's "close enough" (0.001), 1; slot 23's low end (a double,
// 0).
constexpr std::uintptr_t kZero = 0x00A81054, kTieSlack = 0x00A823A4, kHalf = 0x00A84D2C, kThree = 0x00A85074,
                         kSixteen = 0x00A8D45C, kAheadDot = 0x00A99D14, kRatioFloor = 0x00A99E1C,
                         kCloseEnough = 0x00A99E34, kOne = 0x00B09FE8, kZeroDouble = 0x00A80990;
constexpr float kFloatMax = 3.40282347e+38f; // 0x7F7FFFFF
// The return addresses after the game's SC4Random constructions (slots 5
// and 23): clock seeds are counted under them (fixed_seed.h), or under a
// caller's override when one is set, as the game's are.
constexpr std::uint32_t kNearestPathSeedSite = 0x0053E09C, kRandomPointSeedSite = 0x0053DAB4;

std::uint32_t SeedSite(std::uint32_t own)
{
    const std::uint32_t outer = fixed_seed::CurrentSiteOverride();
    return outer ? outer : own;
}

bool Flag(char* self)
{
    return (Virtual<std::uint32_t>(self, slot::kGetFlag) & 0xFFu) != 0;
}

// The wanted key and mask of slots 5, 6 and 7: (type << 16 | from) << 8 |
// to, that is the type's low byte << 24 and from's and to's low bytes, the
// number byte not compared; from 0xFF leaves only the type byte and to, to
// 0xFF takes to out.
std::uint32_t WildMask(std::uint32_t from, std::uint32_t to)
{
    std::uint32_t mask = (from & 0xFFu) == 0xFFu ? 0xFF0000FFu : 0xFF00FFFFu;
    if ((to & 0xFFu) == 0xFFu)
        mask &= 0xFFFFFF00u;
    return mask;
}

std::uint32_t Wanted(std::uint32_t type, std::uint32_t from, std::uint32_t to)
{
    return (((type << 16) | (from & 0xFFu)) << 8) | (to & 0xFFu);
}

// push_back of a word onto a vector of words: in place when there is room
// (nothing written at a null end), else SC4PtrVector::InsertN with the tag
// pointer the game passes.
void PushWord(char** vector, const std::uint32_t* value, const void* tag)
{
    if (vector[1] == vector[2]) {
        CallThis<void>(addr::SC4PtrVector_InsertN, vector, vector[1], value, tag, 1u, 1u);
        return;
    }
    if (vector[1])
        At<std::uint32_t>(vector[1], 0) = *value;
    vector[1] += 4;
}

// push_back of a 12-byte point onto a record's points: in place when there
// is room, else SC4Float3Vector::Insert.
void PushPoint(char* record, const char* point, const void* tag)
{
    char* const where = At<char*>(record, 4);
    if (where == At<char*>(record, 8)) {
        CallThis<void>(addr::SC4Float3Vector_Insert, record, where, point, tag, 1u, 1u);
        return;
    }
    if (where)
        std::memcpy(where, point, 12);
    At<char*>(record, 4) = where + 12;
}

// 0x0053D2E0, slot 19 (point, points), ret 8, this not read: for each
// segment, a line through the point across the segment's direction, 8 to
// either side (0x00A8E178; the direction unscaled when its squared length
// is not above 0), crossed with the segment in x and z
// (SC4::SegmentsIntersect2D): the point's x and z moved to the crossing
// and its y from the segment's by the squared distances' ratio (0 when the
// segment's is below 1e-4); the segment's index. Without a crossing, the
// nearer end (the last only when strictly nearer, or unordered), its index
// (0 or the count less 2). -1 for no points.
std::int32_t __fastcall ProjectOnPath(void*, void*, float* point, char** points)
{
    static bool logged;
    FirstCall("ProjectOnPath", logged);
    if (!points || points[0] == points[1])
        return -1;
    const float* p = reinterpret_cast<const float*>(points[0]);
    float prevX = p[0], prevY = p[1], prevZ = p[2];
    std::int32_t index = 0;
    for (p += 3; p != reinterpret_cast<const float*>(points[1]); p += 3, ++index) {
        const double dx = static_cast<double>(p[0]) - prevX;
        const float dxf = static_cast<float>(dx);
        const float dyf = static_cast<float>(static_cast<double>(p[1]) - prevY);
        const double dz = static_cast<double>(p[2]) - prevZ;
        const float dzf = static_cast<float>(dz);
        const double squared = (static_cast<double>(dzf) * dzf + static_cast<double>(dyf) * dyf) +
                               static_cast<double>(dxf) * dxf;
        // a across x, b across z: the unrounded differences, or scaled to 8
        // (b by the scale unrounded, a by it as a float).
        double a = dz, b = dx;
        if (squared > static_cast<double>(Global<float>(kZero))) {
            const double scale = static_cast<double>(Global<float>(kHalfCell)) /
                                 std::sqrt(static_cast<double>(static_cast<float>(squared)));
            b = scale * dxf;
            a = static_cast<double>(dzf) * static_cast<float>(scale);
        }
        const float across1[2] = {static_cast<float>(a + point[0]), static_cast<float>(point[2] - b)};
        const float across2[2] = {static_cast<float>(point[0] - a), static_cast<float>(b + point[2])};
        const float from[2] = {prevX, prevZ};
        const float to[2] = {p[0], p[2]};
        float crossing[2];
        if (CallCdecl<std::uint8_t>(addr::SC4_SegmentsIntersect2D, static_cast<const float*>(across1),
                                    static_cast<const float*>(across2), static_cast<const float*>(from),
                                    static_cast<const float*>(to), static_cast<float*>(crossing))) {
            point[0] = crossing[0];
            point[2] = crossing[1];
            const float whole = static_cast<float>(
                CallCdecl<double>(addr::SC4_DistanceSquared2D, static_cast<const float*>(from), static_cast<const float*>(to)));
            const double part = CallCdecl<double>(addr::SC4_DistanceSquared2D, static_cast<const float*>(from),
                                                  static_cast<const float*>(crossing));
            const double ratio = !(whole < Global<float>(kRatioFloor)) ? part / whole
                                                                        : static_cast<double>(Global<float>(kZero));
            point[1] = static_cast<float>((static_cast<double>(p[1]) - prevY) * ratio + prevY);
            return index;
        }
        prevX = p[0];
        prevY = p[1];
        prevZ = p[2];
    }
    const float* const first = reinterpret_cast<const float*>(points[0]);
    const float* const last = reinterpret_cast<const float*>(points[1]) - 3;
    const double f0 = static_cast<double>(point[0]) - first[0], f1 = static_cast<double>(point[1]) - first[1],
                 f2 = static_cast<double>(point[2]) - first[2];
    const double l0 = static_cast<double>(point[0]) - last[0], l1 = static_cast<double>(point[1]) - last[1],
                 l2 = static_cast<double>(point[2]) - last[2];
    const double toFirst = std::sqrt((f2 * f2 + f1 * f1) + f0 * f0);
    const double toLast = std::sqrt((l2 * l2 + l1 * l1) + l0 * l0);
    if (!(toLast >= toFirst)) {
        std::memcpy(point, last, 12);
        return static_cast<std::int32_t>(points[1] - points[0]) / 12 - 2;
    }
    std::memcpy(point, first, 12);
    return 0;
}

// 0x0053D6C0, slot 18 (points, point, out, index), ret 0x10: the index
// set to 0 when negative or not below the point count; the path's
// direction (last point less first, to unit length in floats); from the
// index on, the nearest point (y left out when slot 16 says so) of those
// whose direction from the point is ahead (a dot above -0.2) and not at it:
// out, its index. AL whether any was ahead (nearer or not). The last point
// is read before the test for none (an empty vector without storage
// faults).
std::uint32_t __fastcall NearestPointAhead(char* self, void*, char** points, const float* point, float* out,
                                           std::int32_t* index)
{
    static bool logged;
    FirstCall("NearestPointAhead", logged);
    const std::int32_t count = static_cast<std::int32_t>(points[1] - points[0]) / 12;
    if (*index < 0 || static_cast<std::uint32_t>(*index) >= static_cast<std::uint32_t>(count))
        *index = 0;
    // The last and first points are read before the test for none, as the
    // game does (volatile, so the reads stay where they are).
    const volatile float* const first = reinterpret_cast<const float*>(points[0]);
    const volatile float* const last = reinterpret_cast<const float*>(points[1]) - 3;
    const double dx = static_cast<double>(last[0]) - first[0];
    const double dy = static_cast<double>(last[1]) - first[1];
    const double dz = static_cast<double>(last[2]) - first[2];
    const float* const begin = reinterpret_cast<const float*>(points[0]);
    if (begin == reinterpret_cast<const float*>(points[1]))
        return 0;
    const float dzf = static_cast<float>(dz);
    const float unit = static_cast<float>(static_cast<double>(Global<float>(kOne)) /
                                          std::sqrt((dz * dzf + dy * dy) + dx * dx));
    const float ux = static_cast<float>(dx * unit), uy = static_cast<float>(dy * unit),
                uz = static_cast<float>(static_cast<double>(dzf) * unit);
    std::uint32_t ahead = 0;
    float best = kFloatMax;
    std::int32_t i = 0;
    for (const float* p = begin; p != reinterpret_cast<const float*>(points[1]); p += 3, ++i) {
        if (i < *index)
            continue;
        const float vx = static_cast<float>(static_cast<double>(p[0]) - point[0]);
        float vy = static_cast<float>(static_cast<double>(p[1]) - point[1]);
        const float vz = static_cast<float>(static_cast<double>(p[2]) - point[2]);
        if (!Flag(self))
            vy = 0.0f;
        const double distance = std::sqrt((static_cast<double>(vz) * vz + static_cast<double>(vx) * vx) +
                                          static_cast<double>(vy) * vy);
        const float distancef = static_cast<float>(distance);
        if (!(distancef > Global<float>(kZero)))
            continue;
        const double w = static_cast<double>(Global<float>(kOne)) / distance;
        const double dot = (static_cast<double>(uz) * (vz * w) + static_cast<double>(uy) * (vy * w)) +
                           static_cast<double>(ux) * (vx * w);
        if (!(dot > static_cast<double>(Global<float>(kAheadDot))))
            continue;
        ahead = 1;
        if (distancef < best) {
            std::memcpy(out, p, 12);
            best = distancef;
            *index = i;
        }
    }
    return ahead;
}

// 0x0053D890, slot 22 (points, out), ret 8, this not read: the path's
// length (unrounded) halved to a float; walking the segments from the
// first point, the first segment that reaches the half puts out at the
// half along it; the walk does not stop there: the start point and the
// distance walked stay, so each later segment (from that start to its end
// point) that reaches it puts out again. AL whether one did; 0 for none
// or one point.
std::uint32_t __fastcall MiddlePoint(void*, void*, char** points, float* out)
{
    static bool logged;
    FirstCall("MiddlePoint", logged);
    if (!points)
        return 0;
    const float* const pts = reinterpret_cast<const float*>(points[0]);
    const auto count = static_cast<std::uint32_t>(static_cast<std::int32_t>(points[1] - points[0]) / 12);
    if (!count)
        return 0;
    double length = Global<float>(kZero);
    if (count > 1) {
        float prevX = pts[0], prevY = pts[1], prevZ = pts[2];
        for (std::uint32_t i = 1; i < count; ++i) {
            const float* const c = pts + 3 * i;
            const double dx = static_cast<double>(c[0]) - prevX, dy = static_cast<double>(c[1]) - prevY,
                         dz = static_cast<double>(c[2]) - prevZ;
            length = std::sqrt((dz * dz + dy * dy) + dx * dx) + length;
            prevX = c[0];
            prevY = c[1];
            prevZ = c[2];
        }
    }
    const float half = static_cast<float>(length * Global<float>(kHalf));
    float walked = 0.0f;
    float start[3] = {pts[0], pts[1], pts[2]};
    std::uint32_t found = 0;
    if (count <= 1)
        return 0;
    for (std::uint32_t i = 1; i < count; ++i) {
        const float* const c = reinterpret_cast<const float*>(points[0]) + 3 * i;
        const float dxf = static_cast<float>(static_cast<double>(c[0]) - start[0]);
        const float dyf = static_cast<float>(static_cast<double>(c[1]) - start[1]);
        const double dz = static_cast<double>(c[2]) - start[2];
        const float dzf = static_cast<float>(dz);
        const double segment = std::sqrt((dz * dzf + static_cast<double>(dyf) * dyf) + static_cast<double>(dxf) * dxf);
        const float segmentf = static_cast<float>(segment);
        const double reached = segment + walked;
        if (!(reached >= half)) {
            walked = static_cast<float>(reached);
            std::memcpy(start, c, 12);
            continue;
        }
        found = 1;
        const double r = static_cast<double>(Global<float>(kOne)) / segmentf;
        const double ax = dxf * r;
        const float by = static_cast<float>(dyf * r), cz = static_cast<float>(dzf * r);
        const double rest = static_cast<double>(half) - walked;
        const float restf = static_cast<float>(rest);
        const float yoff = static_cast<float>(static_cast<double>(by) * restf);
        const float zoff = static_cast<float>(static_cast<double>(cz) * restf);
        out[2] = static_cast<float>(static_cast<double>(zoff) + start[2]);
        out[0] = static_cast<float>(rest * ax + start[0]);
        out[1] = static_cast<float>(static_cast<double>(yoff) + start[1]);
    }
    return found;
}

// 0x0053DA80, slot 23 (key, out), ret 8: the key's record (slot 3, the
// length into a zeroed float); a distance drawn (an SC4Random of the clock
// seed -1, Uniform 0 to the length); walking the segments, the first longer
// than what is left puts out that far along it, AL 1. AL 0 when none is
// (or no record). The segment count is the point count less 1, unsigned:
// an empty record walks on past its points.
std::uint32_t __fastcall RandomPointOnPath(char* self, void*, std::uint32_t key, float* out)
{
    static bool logged;
    FirstCall("RandomPointOnPath", logged);
    float length = 0.0f;
    char** const points = Virtual<char**>(self, slot::kGetPath, key, &length);
    if (!points)
        return 0;
    alignas(8) char random[0x18];
    {
        const fixed_seed::SiteOverride site(SeedSite(kRandomPointSeedSite));
        CallThis<void*>(addr::SC4Random_Construct, random, static_cast<std::uint32_t>(0xFFFFFFFF));
    }
    double left = CallThis<double>(addr::SC4Random_Uniform, random, Global<double>(kZeroDouble),
                                   static_cast<double>(length));
    const float* const pts = reinterpret_cast<const float*>(points[0]);
    const auto segments = static_cast<std::uint32_t>(static_cast<std::int32_t>(points[1] - points[0]) / 12 - 1);
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float* const a = pts + 3 * i;
        const float* const b = a + 3;
        const float dxf = static_cast<float>(static_cast<double>(b[0]) - a[0]);
        const float dyf = static_cast<float>(static_cast<double>(b[1]) - a[1]);
        const double dz = static_cast<double>(b[2]) - a[2];
        const float dzf = static_cast<float>(dz);
        const double segment = std::sqrt((dz * dzf + static_cast<double>(dyf) * dyf) + static_cast<double>(dxf) * dxf);
        if (left < segment) {
            // The direction from the floats' squares this time.
            const double r = static_cast<double>(Global<float>(kOne)) /
                             std::sqrt((static_cast<double>(dzf) * dzf + static_cast<double>(dyf) * dyf) +
                                       static_cast<double>(dxf) * dxf);
            const float xoff = static_cast<float>(dxf * r * left);
            const float by = static_cast<float>(dyf * r), cz = static_cast<float>(dzf * r);
            const float yoff = static_cast<float>(by * left), zoff = static_cast<float>(cz * left);
            out[0] = static_cast<float>(static_cast<double>(xoff) + a[0]);
            out[1] = static_cast<float>(static_cast<double>(yoff) + a[1]);
            out[2] = static_cast<float>(static_cast<double>(zoff) + a[2]);
            return 1;
        }
        left = left - segment;
    }
    return 0;
}

// 0x0053DF70, slot 5 (type, from, to, point, key out, length out), ret
// 0x18: over the paths whose key matches (WildMask), each point's distance
// from the point (y left out when slot 16 says so); a nearer one (the
// unrounded distance below the best float), or one within 0.01 of the best
// that wins a coin (a new SC4Random of the clock seed -1, one step below
// 0x7FFFFFFF), becomes the best: its record, key and length. The best
// record, or null.
char* __fastcall NearestPath(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                             const float* point, std::uint32_t* keyOut, std::uint32_t* lengthOut)
{
    static bool logged;
    FirstCall("NearestPath", logged);
    const std::uint32_t mask = WildMask(from, to);
    const std::uint32_t wanted = Wanted(type, from, to) & mask;
    char* found = nullptr;
    float best = kFloatMax;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if ((key & mask) != wanted)
            continue;
        char* const record = node + kNodeRecord;
        for (const float* p = At<float*>(record, 0); p != At<float*>(record, 4); p += 3) {
            const float dx = static_cast<float>(static_cast<double>(point[0]) - p[0]);
            const float dy = static_cast<float>(static_cast<double>(point[1]) - p[1]);
            const float dz = static_cast<float>(static_cast<double>(point[2]) - p[2]);
            const float y = Flag(self) ? dy : Global<float>(kZero);
            const double distance = std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dx) * dx) +
                                              static_cast<double>(y) * y);
            const float distancef = static_cast<float>(distance);
            bool take = distance < best;
            if (!take && std::fabs(static_cast<double>(distancef) - best) < Global<float>(kTieSlack)) {
                alignas(8) char random[0x18];
                {
                    const fixed_seed::SiteOverride site(SeedSite(kNearestPathSeedSite));
                    CallThis<void*>(addr::SC4Random_Construct, random, static_cast<std::uint32_t>(0xFFFFFFFF));
                }
                take = CallThis<std::uint32_t>(addr::cRZRandom_Advance, random) < 0x7FFFFFFFu;
            }
            if (!take)
                continue;
            best = distancef;
            found = record;
            if (keyOut)
                *keyOut = key;
            if (lengthOut)
                *lengthOut = At<std::uint32_t>(record, kRecordLength);
        }
    }
    return found;
}

// Slots 6 and 7: the keys of the paths (WildMask) whose first (6) or last
// (7) point is within the radius of the point (the unrounded distance at
// most the radius), the nearest so far put at the front (SC4PtrVector::
// Insert; the first one pushed), the others pushed at the end; the
// vector copied into the answer (SC4Uint32Vector::Copy) and freed. The
// point is read with no test for an empty record.
void* PathsNear(char* self, void* result, std::uint32_t type, const float* point, std::uint32_t from,
                std::uint32_t to, float radius, bool last)
{
    char* keys[3] = {};
    float best = kFloatMax;
    const std::uint32_t mask = WildMask(from, to);
    const std::uint32_t wanted = Wanted(type, from, to) & mask;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if ((key & mask) != wanted)
            continue;
        const float* const p = last ? At<float*>(node, kNodeRecord + 4) - 3 : At<float*>(node, kNodeRecord);
        const double dx = static_cast<double>(p[0]) - point[0], dy = static_cast<double>(p[1]) - point[1],
                     dz = static_cast<double>(p[2]) - point[2];
        const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        float distancef = static_cast<float>(distance);
        if (!(distance <= radius))
            continue;
        const std::uint32_t value = key;
        if (distancef < best) {
            if (keys[0] != keys[1])
                CallThis<void*>(addr::SC4PtrVector_Insert, keys, keys[0], &value);
            else
                PushWord(keys, &value, &distancef);
            best = distancef;
        } else {
            PushWord(keys, &value, &distancef);
        }
    }
    CallThis<void*>(addr::SC4Uint32Vector_Copy, result, keys);
    if (keys[0])
        CallCdecl<void>(addr::SC4_Free, keys[0]);
    return result;
}

// 0x0053E150, slot 6 (result, type, point, from, to, radius), ret 0x18:
// PathsNear by the first points; the result.
void* __fastcall PathsNearStart(char* self, void*, void* result, std::uint32_t type, const float* point,
                                std::uint32_t from, std::uint32_t to, float radius)
{
    static bool logged;
    FirstCall("PathsNearStart", logged);
    return PathsNear(self, result, type, point, from, to, radius, false);
}

// 0x0053E350, slot 7 (result, type, point, from, to, radius), ret 0x18:
// PathsNear by the last points; the result.
void* __fastcall PathsNearEnd(char* self, void*, void* result, std::uint32_t type, const float* point,
                              std::uint32_t from, std::uint32_t to, float radius)
{
    static bool logged;
    FirstCall("PathsNearEnd", logged);
    return PathsNear(self, result, type, point, from, to, radius, true);
}

// 0x0053FA40, slot 8 (type, from, to, generator, length out), ret 0x14:
// the records of the paths whose key with the number left out is
// (type << 16 | from) << 8 | to (no wild cards); one drawn from the
// caller's generator (SC4Random::Below) and its length. The record, or
// null.
char* __fastcall RandomPath(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                            void* generator, std::uint32_t* lengthOut)
{
    static bool logged;
    FirstCall("RandomPath", logged);
    const std::uint32_t wanted = Wanted(type, from, to);
    const Iterator it = First(self);
    if (!it.node)
        return nullptr;
    char* records[3] = {};
    std::uint32_t count = 0;
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        if ((At<std::uint32_t>(node, kNodeKey) & 0xFF00FFFFu) != wanted)
            continue;
        ++count;
        const auto record = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node + kNodeRecord));
        PushWord(records, &record, &record);
    }
    char* answer = nullptr;
    if (count) {
        const std::uint32_t i = CallThis<std::uint32_t>(addr::SC4Random_Below, generator, count);
        answer = At<char*>(records[0], i * 4);
        if (lengthOut)
            *lengthOut = At<std::uint32_t>(answer, kRecordLength);
    }
    if (records[0])
        CallCdecl<void>(addr::SC4_Free, records[0]);
    return answer;
}

// 0x0053FB80, slot 9 (type, from, to, generator, key out, length out), ret
// 0x18: as slot 8 with wild cards: type 0 leaves the type nibble (and the
// number) out but keeps the high nibble 0, from or to 0xFF leave theirs
// out; the keys gathered too when the key is wanted. The drawn record, its
// length and key, or null.
char* __fastcall RandomPathKeyed(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to,
                                 void* generator, std::uint32_t* keyOut, std::uint32_t* lengthOut)
{
    static bool logged;
    FirstCall("RandomPathKeyed", logged);
    std::uint32_t mask = type ? 0xFF00FFFFu : 0xF000FFFFu;
    if ((from & 0xFFu) == 0xFFu)
        mask &= 0xFFFF00FFu;
    if ((to & 0xFFu) == 0xFFu)
        mask &= 0xFFFFFF00u;
    const std::uint32_t wanted = Wanted(type, from, to) & mask;
    const Iterator it = First(self);
    if (!it.node)
        return nullptr;
    char* records[3] = {};
    char* keys[3] = {};
    std::uint32_t count = 0;
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t* const key = &At<std::uint32_t>(node, kNodeKey);
        if ((*key & mask) != wanted)
            continue;
        ++count;
        const auto record = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node + kNodeRecord));
        PushWord(records, &record, &wanted);
        if (keyOut)
            PushWord(keys, key, &wanted);
    }
    char* answer = nullptr;
    if (count) {
        const std::uint32_t i = CallThis<std::uint32_t>(addr::SC4Random_Below, generator, count);
        answer = At<char*>(records[0], i * 4);
        if (lengthOut)
            *lengthOut = At<std::uint32_t>(answer, kRecordLength);
        if (keyOut)
            *keyOut = At<std::uint32_t>(keys[0], i * 4);
    }
    if (keys[0])
        CallCdecl<void>(addr::SC4_Free, keys[0]);
    if (records[0])
        CallCdecl<void>(addr::SC4_Free, records[0]);
    return answer;
}

// 0x0053ECE0, slot 20 (point, type, key out), ret 0xC: each path (type high
// nibble 0; of the type nibble unless 0) asked through slot 19 for the
// point projected on it (a copy of the point as given); the nearest
// projection (the unrounded distance below the best float) goes into the
// point, its key out; within 0.001 it is the answer at once. The best
// projection's index, -1 for none.
std::int32_t __fastcall NearestOnPaths(char* self, void*, float* point, std::uint32_t type, std::uint32_t* keyOut)
{
    static bool logged;
    FirstCall("NearestOnPaths", logged);
    const float start = Global<float>(kStartFloat);
    float projected[3] = {start, start, start};
    const float given[3] = {point[0], point[1], point[2]};
    std::int32_t answer = -1;
    float best = kFloatMax;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (key & 0xF0000000u)
            continue;
        if (type && ((key >> 24) & 0xFu) != type)
            continue;
        std::memcpy(projected, given, sizeof projected);
        const std::int32_t index = Virtual<std::int32_t>(self, slot::kProjectOnPath, projected, node + kNodeRecord);
        if (index == -1)
            continue;
        const double dx = static_cast<double>(projected[0]) - given[0],
                     dy = static_cast<double>(projected[1]) - given[1],
                     dz = static_cast<double>(projected[2]) - given[2];
        const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        if (!(distance < best))
            continue;
        std::memcpy(point, projected, sizeof projected);
        best = static_cast<float>(distance);
        if (keyOut)
            *keyOut = key;
        answer = index;
        if (distance <= Global<float>(kCloseEnough))
            return index;
    }
    return answer;
}

// 0x0053EE90, slot 21 (type, point, out, key out), ret 0x10: each path or
// stop (of the type nibble unless 0) asked through slot 22 for its middle
// point (into one point kept across the walk); the farthest (strictly
// above the best so far, from 0; y left out when slot 16 says so) into out
// with its key. AL whether there was one.
std::uint32_t __fastcall FarthestMiddle(char* self, void*, std::uint32_t type, const float* point, float* out,
                                        std::uint32_t* keyOut)
{
    static bool logged;
    FirstCall("FarthestMiddle", logged);
    const float start = Global<float>(kStartFloat);
    float middle[3] = {start, start, start};
    std::uint32_t found = 0;
    float best = 0.0f;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (type && ((key >> 24) & 0xFu) != type)
            continue;
        if (!(Virtual<std::uint32_t>(self, slot::kMiddlePoint, node + kNodeRecord, middle) & 0xFFu))
            continue;
        const float dx = static_cast<float>(static_cast<double>(point[0]) - middle[0]);
        const float dy = static_cast<float>(static_cast<double>(point[1]) - middle[1]);
        const float dz = static_cast<float>(static_cast<double>(point[2]) - middle[2]);
        const float y = Flag(self) ? dy : Global<float>(kZero);
        const double distance = std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(y) * y) +
                                          static_cast<double>(dx) * dx);
        if (!(distance > best))
            continue;
        best = static_cast<float>(distance);
        std::memcpy(out, middle, sizeof middle);
        if (keyOut)
            *keyOut = key;
        found = 1;
    }
    return found;
}

// 0x0053F100, slot 37 (type, from, to), ret 0xC: the paths whose key with
// the number left out is (type << 16 | from) << 8 | to and that have
// points, listed with their first points (a list of SC4::stl_allocate
// nodes, freed after); the box of those points in x and z; along the wider
// side (x unless z's is strictly wider; x's width unrounded, z's as a
// float) its middle; the key whose first point is nearest the middle along
// that side. With none, the packed key itself (number 0).
std::uint32_t __fastcall MiddleKey(char* self, void*, std::uint32_t type, std::uint32_t from, std::uint32_t to)
{
    static bool logged;
    FirstCall("MiddleKey", logged);
    char* const head = CallCdecl<char*>(addr::SC4_stl_allocate, 0x18u);
    std::uint32_t answer = Wanted(type, from, to);
    const std::uint32_t wanted = answer & 0xFF00FFFFu;
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    bool any = false;
    float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if ((key & 0xFF00FFFFu) != wanted)
            continue;
        const float* const p = At<float*>(node, kNodeRecord);
        if (p == At<float*>(node, kNodeRecord + 4))
            continue;
        char* const item = CallCdecl<char*>(addr::SC4_stl_allocate, 0x18u);
        At<std::uint32_t>(item, 8) = key;
        std::memcpy(item + 0xC, p, 12);
        char* const tail = At<char*>(head, 4);
        At<char*>(item, 0) = head;
        At<char*>(item, 4) = tail;
        At<char*>(tail, 0) = item;
        At<char*>(head, 4) = item;
        if (!any) {
            minX = maxX = p[0];
            minZ = maxZ = p[2];
            any = true;
            continue;
        }
        if (p[0] < minX)
            minX = p[0];
        if (p[2] < minZ)
            minZ = p[2];
        if (p[0] > maxX)
            maxX = p[0];
        if (p[2] > maxZ)
            maxZ = p[2];
    }
    if (any) {
        const double wideX = static_cast<double>(maxX) - minX;
        const double wideZ = static_cast<double>(maxZ) - minZ;
        const float wideZf = static_cast<float>(wideZ);
        const bool alongX = !(std::fabs(wideX) < std::fabs(wideZ));
        const double middle = alongX ? wideX * Global<float>(kHalf) + minX
                                     : static_cast<double>(wideZf) * Global<float>(kHalf) + minZ;
        float best = kFloatMax;
        for (char* item = At<char*>(head, 0); item != head; item = At<char*>(item, 0)) {
            const double along = alongX ? At<float>(item, 0xC) : At<float>(item, 0x14);
            const double off = std::fabs(along - middle);
            if (off < best) {
                best = static_cast<float>(off);
                answer = At<std::uint32_t>(item, 8);
            }
        }
    }
    for (char* item = At<char*>(head, 0); item != head;) {
        char* const next = At<char*>(item, 0);
        CallCdecl<void>(addr::SC4_Free, item);
        item = next;
    }
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    CallCdecl<void>(addr::SC4_Free, head);
    return answer;
}

// 0x0053F360, slot 39 (key, point, direction, threshold, out, key out,
// index out), ret 0x1C: a probe from the point 48 along the direction (x
// and y by 16 unrounded, z by 16 as a float, then 3, as floats). For the key's path (none: AL 0, nothing written)
// its direction (last point less first, unit in floats); over the other
// paths of the same type byte whose direction's dot with it is at least the
// threshold (or unordered), each segment crossed with the probe in x and z
// (SC4::SegmentsIntersect2D): the crossing nearest the point (SC4::
// Distance2D) puts out (the point's y), its key and its segment's index
// (written as it is found). AL whether one crossed; the key out then. The
// last points are read with no test for empty records.
std::uint32_t __fastcall CrossingPath(char* self, void*, std::uint32_t key, const float* point,
                                      const float* direction, float threshold, float* out, std::uint32_t* keyOut,
                                      std::int32_t* indexOut)
{
    static bool logged;
    FirstCall("CrossingPath", logged);
    const float start[2] = {point[0], point[2]};
    const double sixteen = Global<float>(kSixteen), three = Global<float>(kThree);
    const double dx16 = static_cast<double>(direction[0]) * sixteen;
    const double dy16 = static_cast<double>(direction[1]) * sixteen;
    const float dz16 = static_cast<float>(static_cast<double>(direction[2]) * sixteen);
    std::uint32_t found = 0;
    float best = kFloatMax;
    const float ax = static_cast<float>(dx16 * three), ay = static_cast<float>(dy16 * three),
                az = static_cast<float>(static_cast<double>(dz16) * three);
    float result[3] = {static_cast<float>(static_cast<double>(ax) + point[0]),
                       static_cast<float>(static_cast<double>(ay) + point[1]),
                       static_cast<float>(static_cast<double>(az) + point[2])};
    const float end[2] = {result[0], result[2]};
    char* const own = FindNode(self, key);
    if (!own)
        return 0;
    const float* const ob = At<float*>(own, kNodeRecord);
    const float* const oe = At<float*>(own, kNodeRecord + 4) - 3;
    const double ux0 = static_cast<double>(oe[0]) - ob[0], uy0 = static_cast<double>(oe[1]) - ob[1],
                 uz0 = static_cast<double>(oe[2]) - ob[2];
    const float uz0f = static_cast<float>(uz0);
    const float unit = static_cast<float>(static_cast<double>(Global<float>(kOne)) /
                                          std::sqrt((uz0 * uz0f + uy0 * uy0) + ux0 * ux0));
    const float ux = static_cast<float>(ux0 * unit), uy = static_cast<float>(uy0 * unit),
                uz = static_cast<float>(static_cast<double>(uz0f) * unit);
    std::uint32_t bestKey = 0;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t other = At<std::uint32_t>(node, kNodeKey);
        if (other == key || ((other ^ key) & 0xF0000000u) || ((other ^ key) & 0x0F000000u))
            continue;
        const float* const pb = At<float*>(node, kNodeRecord);
        const float* const pe = At<float*>(node, kNodeRecord + 4) - 3;
        const double vx0 = static_cast<double>(pe[0]) - pb[0], vy0 = static_cast<double>(pe[1]) - pb[1],
                     vz0 = static_cast<double>(pe[2]) - pb[2];
        const float vz0f = static_cast<float>(vz0);
        const float w = static_cast<float>(static_cast<double>(Global<float>(kOne)) /
                                           std::sqrt((vz0 * vz0f + vy0 * vy0) + vx0 * vx0));
        const float vx = static_cast<float>(vx0 * w);
        const double vy = vy0 * w, vz = static_cast<double>(vz0f) * w;
        const double dot = (static_cast<double>(uz) * vz + vy * uy) + static_cast<double>(vx) * ux;
        if (dot < threshold)
            continue;
        float prev[2] = {pb[0], pb[2]};
        std::int32_t i = 0;
        for (const float* p = pb + 3; p != At<float*>(node, kNodeRecord + 4); p += 3, ++i) {
            const float from[2] = {prev[0], prev[1]};
            const float to[2] = {p[0], p[2]};
            float crossing[2];
            if (CallCdecl<std::uint8_t>(addr::SC4_SegmentsIntersect2D, static_cast<const float*>(from),
                                        static_cast<const float*>(to), static_cast<const float*>(start),
                                        static_cast<const float*>(end), static_cast<float*>(crossing))) {
                const double distance = CallCdecl<double>(addr::SC4_Distance2D, static_cast<const float*>(crossing),
                                                          static_cast<const float*>(start));
                if (distance < best) {
                    best = static_cast<float>(distance);
                    result[0] = crossing[0];
                    result[1] = point[1];
                    result[2] = crossing[1];
                    bestKey = other;
                    *indexOut = i;
                    found = 1;
                }
            }
            prev[0] = p[0];
            prev[1] = p[2];
        }
    }
    if (found) {
        *keyOut = bestKey;
        std::memcpy(out, result, sizeof result);
    }
    return found;
}

// 0x005425C0, slot 31 (number, type, new type, reverse), ret 0x10: each
// path or stop (of the type nibble unless 0; of the number byte unless
// 0xFF), in the table's order as it grows, copied under the key with the
// new type ORed under its high nibble ((high << 4 | new type) << 24, the
// new type's higher bits shifted out), the same number, from and to
// (swapped when the reverse byte is set): when that key is not in the
// table, AddPath (at the game's address), the points reserved and copied
// (reversed with the sides), the length and byte. The count copied. Copies
// are met by the walk too.
std::uint32_t __fastcall CopyPathsAsType(char* self, void*, std::uint32_t number, std::uint32_t type,
                                         std::uint32_t newType, std::uint32_t reverse)
{
    static bool logged;
    FirstCall("CopyPathsAsType", logged);
    std::uint32_t count = 0;
    const Iterator it = First(self);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        const std::uint32_t key = At<std::uint32_t>(node, kNodeKey);
        if (type && type != ((key >> 24) & 0xFu))
            continue;
        const std::uint32_t own = (key >> 16) & 0xFFu;
        if ((number & 0xFFu) != 0xFFu && (number & 0xFFu) != own)
            continue;
        std::uint32_t from = (key >> 8) & 0xFFu, to = key & 0xFFu;
        if (reverse & 0xFFu) {
            const std::uint32_t side = from;
            from = to;
            to = side;
        }
        const std::uint32_t copy = (((((((key >> 28) << 4) | newType) << 8) | own) << 8 | from) << 8) | to;
        if (FindNode(self, copy))
            continue;
        char* const record = CallThis<char*>(addr::cSC4PathInfo_AddPath, self, copy);
        if (!record)
            continue;
        const auto bytes = static_cast<std::int32_t>(At<char*>(node, kNodeRecord + 4) - At<char*>(node, kNodeRecord));
        CallThis<void>(addr::cSC4PathInfo_ReservePoints, record, static_cast<std::uint32_t>(bytes / 12));
        if (reverse & 0xFFu) {
            for (char* p = At<char*>(node, kNodeRecord + 4); p != At<char*>(node, kNodeRecord); p -= 12)
                PushPoint(record, p - 12, &reverse);
        } else {
            for (char* p = At<char*>(node, kNodeRecord); p != At<char*>(node, kNodeRecord + 4); p += 12)
                PushPoint(record, p, &reverse);
        }
        At<std::uint32_t>(record, kRecordLength) = At<std::uint32_t>(node, kNodeLength);
        ++count;
        At<std::uint8_t>(record, kRecordByte) = At<std::uint8_t>(node, 0x18);
    }
    return count;
}

// ====================================================================================
// Install

bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_AUTOMATA_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("automata pathinfo %s: no address entry for 0x%08X", what,
                       static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("automata pathinfo %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Part pi1.
const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4PathInfoFactory_GetClassId, FactoryGetClassId),
    OPENSC4_DETOUR(addr::cSC4PathInfoFactory_Create, FactoryCreate),
    OPENSC4_DETOUR(addr::cSC4PathInfoFactory_DeletingDestructor, FactoryDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PathInfoFactory_CreateInstance, FactoryCreateInstance),
    OPENSC4_DETOUR(addr::cSC4PathInfo_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4PathInfo_PoolNewPage, PoolNewPage),
    OPENSC4_DETOUR(addr::cSC4PathInfo_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4PathInfo_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CopyConstruct, CopyConstruct),
    OPENSC4_DETOUR(addr::cSC4PathInfo_Clone, Clone),
    OPENSC4_DETOUR(addr::cSC4PathInfo_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetPath, GetPath),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetPathByParts, GetPathByParts),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetPathLength, GetPathLength),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetPathLengthByParts, GetPathLengthByParts),
    OPENSC4_DETOUR(addr::cSC4PathInfo_HasPaths, HasPaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetFlag, GetFlag),
    OPENSC4_DETOUR(addr::cSC4PathInfo_SetFlag, SetFlag),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetStopPointByParts, GetStopPointByParts),
    OPENSC4_DETOUR(addr::cSC4PathInfo_AddPath, AddPath),
    OPENSC4_DETOUR(addr::cSC4PathInfo_AddStop, AddStop),
    OPENSC4_DETOUR(addr::cSC4PathInfo_AddPathByParts, AddPathByParts),
    OPENSC4_DETOUR(addr::cSC4PathInfo_AddStopByParts, AddStopByParts),
    OPENSC4_DETOUR(addr::cSC4PathInfo_FreePathNumber, FreePathNumber),
    OPENSC4_DETOUR(addr::cSC4PathInfo_ReservePoints, ReservePoints),
};

// Part pi2.
const Detour kTables[] = {
    OPENSC4_DETOUR(addr::cSC4PathInfoFactory_ReadPaths, ReadPaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_ReadLine, ReadLine),
    OPENSC4_DETOUR(addr::cSC4PathInfo_ReadNumber, ReadNumber),
    OPENSC4_DETOUR(addr::cSC4PathInfo_ReadPoint, ReadPoint),
    OPENSC4_DETOUR(addr::cSC4PathInfo_MirrorType2, MirrorType2),
    OPENSC4_DETOUR(addr::cSC4PathInfo_InsertUnique, InsertUnique),
    OPENSC4_DETOUR(addr::cSC4PathInfo_InsertEqual, InsertEqual),
    OPENSC4_DETOUR(addr::cSC4PathInfo_InsertNew, InsertNew),
    OPENSC4_DETOUR(addr::cSC4PathInfo_FindOrInsert, FindOrInsert),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CopyPaths, CopyPaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CountPaths, CountPaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_KeysFrom, KeysFrom),
    OPENSC4_DETOUR(addr::cSC4PathInfo_KeysTo, KeysTo),
    OPENSC4_DETOUR(addr::cSC4PathInfo_ConnectPaths, ConnectPaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_MovePaths, MovePaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_JoinEnds, JoinEnds),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CountStopsOfKey, CountStopsOfKey),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CountStops, CountStops),
    OPENSC4_DETOUR(addr::cSC4PathInfo_GetStopPoint, GetStopPoint),
    OPENSC4_DETOUR(addr::cSC4PathInfo_FlaggedKeys, FlaggedKeys),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CheckEnds, CheckEnds),
    OPENSC4_DETOUR(addr::cSC4PathInfo_HighestNumber, HighestNumber),
};

// Part pi3a.
const Detour kSearches[] = {
    OPENSC4_DETOUR(addr::cSC4PathInfo_NearestPath, NearestPath),
    OPENSC4_DETOUR(addr::cSC4PathInfo_PathsNearStart, PathsNearStart),
    OPENSC4_DETOUR(addr::cSC4PathInfo_PathsNearEnd, PathsNearEnd),
    OPENSC4_DETOUR(addr::cSC4PathInfo_RandomPath, RandomPath),
    OPENSC4_DETOUR(addr::cSC4PathInfo_RandomPathKeyed, RandomPathKeyed),
    OPENSC4_DETOUR(addr::cSC4PathInfo_NearestPointAhead, NearestPointAhead),
    OPENSC4_DETOUR(addr::cSC4PathInfo_ProjectOnPath, ProjectOnPath),
    OPENSC4_DETOUR(addr::cSC4PathInfo_NearestOnPaths, NearestOnPaths),
    OPENSC4_DETOUR(addr::cSC4PathInfo_FarthestMiddle, FarthestMiddle),
    OPENSC4_DETOUR(addr::cSC4PathInfo_MiddlePoint, MiddlePoint),
    OPENSC4_DETOUR(addr::cSC4PathInfo_RandomPointOnPath, RandomPointOnPath),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CopyPathsAsType, CopyPathsAsType),
    OPENSC4_DETOUR(addr::cSC4PathInfo_MiddleKey, MiddleKey),
    OPENSC4_DETOUR(addr::cSC4PathInfo_CrossingPath, CrossingPath),
};

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object") &&
           InstallList(kTables, sizeof kTables / sizeof kTables[0], "tables") &&
           InstallList(kSearches, sizeof kSearches / sizeof kSearches[0], "searches");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.pathinfo", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PathInfo_Construct,
                        "Our path information (the network pieces' paths, resource type 0x296678F7) and its "
                        "resource factory: the factory's class ID, Create, deleting destructor and "
                        "CreateInstance; the pool's free and new page; the constructor, deleting destructor, copy "
                        "constructor, Clone and QueryInterface; slots 3, 4, 10, 11, 13, 16, 17 and 36; AddPath, "
                        "AddStop and their packed forms, the free path number and the points' reserve (part "
                        "pi1); the SC4PATHS reader (the factory's slot 10) with its line, number and point readers "
                        "and the type 2 mirror, the path table's insert_unique, insert_equal, insert, operator[] "
                        "and copy, slots 12, 14, 15, 27, 28, 32 to 35, 38 and 40 and the highest number (part "
                        "pi2); the searches, slots 5 to 9, 18 to 23, 31, 37 and 39 (part pi3a) (61 detours; "
                        "src/core/automata_pathinfo.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_pathinfo
