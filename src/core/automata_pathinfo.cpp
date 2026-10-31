// Our path information, part pi1 (build 641): cSC4PathInfo, the resource of
// type 0x296678F7 (interface 0xA9667793) that holds a network piece's paths
// (the SC4PATHS files), and cSC4PathInfoFactory, its resource factory (class
// ID 0x496678FE). pi1: the factory (its class ID, Create, deleting destructor
// and CreateInstance), the pool's free and new page, the object (constructor,
// deleting destructor, copy constructor, Clone, QueryInterface), the small
// queries (slots 3, 4, 10, 11, 13, 16, 17 and 36) and the building (AddPath,
// AddStop and their packed forms, the free path number, the points' reserve).
// The survey is in docs/automata.md, "Part pi". Calls between functions of
// the part go to the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.pathinfo: the 25 detours in kObject below.
//
// What it does. A path information is a 0x3C-byte block of the pool
// 0x00B09FEC (4 KB pages of 0x44 blocks): +0 the interface (vtable
// 0x00A99E70), +4 the resource part (cRZResource, vtable 0x00A99E38: its
// reference count +8, its key), +0x1C the path table (a hash map of 100
// buckets from the key to a record), +0x30 the flags (1 slot 16's and 17's,
// 2 there are stops), +0x31 a byte per path type (0 to 7) that says it has
// one. A key is type << 24 | number << 16 | from << 8 | to, the type's high
// nibble 1 or 2 for a stop. A node is 0x1C bytes: next, key, the record
// (+8: the points, a vector of 12-byte points; +0x14 the length; +0x18 a
// byte). The factory's CreateInstance makes one for the type 0x296678F7;
// its slot 10 (0x00542900, the game's) reads an SC4PATHS file into it
// through AddPathByParts and AddStopByParts.
//
// Kept as the game's: the adjustors 0x005403D0 (+4 slot 0) and 0x005403E0
// (+4 slot 12), AddRef and Release (0x00808600, 0x00808610, shared), the
// path table's code (insert_unique 0x0053FF10, the copy 0x00540160, clear,
// resize), the points' allocate-and-copy 0x0053D5A0 (also called from
// 0x0050C540), the base classes' constructors and destructors, the slot
// GetStopPointByParts calls (35, 0x0053E740) and everything of pi2 on.
//
// Oddities kept: QueryInterface holds through the object's slot 1 whatever
// it answers; 0x456B8F1D on a null object answers null and 0; HasPaths reads
// +0x31 + type with no bound (the type 0 is the stop flag 2); AddStop's
// value carries a length and a byte from the game's stack it never set
// (stored only when the insert fails to happen; ours 0); FreePathNumber
// compares the number signed (a "negative" one starts from 0 by its low
// byte) and stops at 26 with that key even when it is taken; the packed
// forms keep only the low byte of each part (the type's too, after the
// stop's 0x10 or 0x20 is ORed in). AL only in the game: CreateInstance,
// QueryInterface, HasPaths, AddStop; ours zero-extends.
//
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

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

#undef OPENSC4_DETOUR

bool InstallObject(registry::Mode)
{
    return InstallList(kObject, sizeof kObject / sizeof kObject[0], "object");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.pathinfo", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PathInfo_Construct,
                        "Our path information (the network pieces' paths, resource type 0x296678F7) and its "
                        "resource factory: the factory's class ID, Create, deleting destructor and "
                        "CreateInstance; the pool's free and new page; the constructor, deleting destructor, copy "
                        "constructor, Clone and QueryInterface; slots 3, 4, 10, 11, 13, 16, 17 and 36; AddPath, "
                        "AddStop and their packed forms, the free path number and the points' reserve (25 "
                        "detours, part pi1; src/core/automata_pathinfo.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::automata_pathinfo
