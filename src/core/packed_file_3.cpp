// The framework's packed file, cGZDBSegmentPackedFile (docs/resources.md),
// read side, part 3: 0x0096FB0F to 0x009710F2. Replacements, off by default
// (resources.stale_record_fix, a fix of the original's bug, is always on):
//
//   resources.packed_file.part3: checking the index against the file's
//     length, reading the holes table, reading a compressed record into a
//     caller's buffer, the uncompressed size getters, the index insert that
//     grows the table first, the destructor and the deleting destructor,
//     opening a record for reading, GetRecordSize, ReadRecord and the raw
//     read.
//   resources.stale_record_fix: the game's rare crash at 0x0096DA20, fixed
//     (docs/crashes.md). A read-open of a key that is already open shares
//     the open record's data. Now and then the first node of the open-record
//     map for the key points at a record that is closed (flags word 0) and
//     has no buffer any more, so the new record's constructor (0x0096D9D1)
//     increments a count through NULL. With the fix on, every such node at
//     the front of the key's range is erased before the open looks the key
//     up, without touching the record it points at; the open then reads the
//     record from the file again. The public OpenRecord (0x009757A0) holds
//     the segment's lock around the open, so the map is ours to change.
//
// Both settings put the same detour on OpenRecordUnlocked (0x009701FD), our
// translation of it; the fix is a step at its top behind `g_staleRecordFix`.
//
// The object (0x1C8 bytes) has its main vtable 0x00AD87D0 at +0 and the
// cIGZPersistDBSegment interface (vtable 0x00AD8728) at +4. Offsets from the
// object's start, as far as this part uses them:
//
//   +0x0C   the lock (slot +4 locks, slot +8 unlocks)
//   +0x1C   open flags: bit 0 read, bit 1 write
//   +0x24   the index: a pointer to a hash table from key to {offset, size}
//           (nodes: +0 next, +4 key, +0x10 offset, +0x14 size)
//   +0x28   index dirty
//   +0x2C   the path (a game string)
//   +0x40   the file (an embedded cRZFile)
//   +0xC0   where the package starts inside the file
//   +0xD4   the size up to which a record is read into memory when opened
//   +0xD8   the open records: a multimap from key to record (nodes: +0x10
//           key, +0x1C record)
//   +0x114  hole count; +0x118 hole table offset (from the header as read)
//   +0x14C  the holes map {offset, size}; +0x158 holes dirty
//   +0x15C, +0x170, +0x188, +0x19C  four hash tables of dwords
//   +0x1B0  the directory: a hash table from key to uncompressed size, one
//           entry per compressed record (nodes: +0 next, +4 key, +0x10 size)
//
// A record: +0x04 its interface, +0x0A flags word, +0x40 kind (1 read from
// the file on demand: +0x44 offset, +0x4C size; 2 in memory: +0x44 a shared
// {data*, count}, +0x4C size).
//
// Still the game's in this range: the write side (0x0096FB0F, 0x0096FDE2,
// 0x0096FE20, 0x0096FF01, 0x00970080, 0x00970193, 0x0097060C, 0x009708BE to
// 0x00970F0F) and the six interface thunks 0x00970163 to 0x0097018B
// (`sub ecx,4; jmp`: no room for a detour).
#include "core/address_check.h"
#include "core/crash.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::packed_file {
namespace {

namespace object { // offsets from the object's start
constexpr std::size_t kInterface = 0x04;
constexpr std::size_t kLock = 0x0C;
constexpr std::size_t kOpenFlags = 0x1C;
constexpr std::size_t kIndex = 0x24;
constexpr std::size_t kPath = 0x2C;
constexpr std::size_t kFile = 0x40;
constexpr std::size_t kBaseOffset = 0xC0;
constexpr std::size_t kMemoryLimit = 0xD4;
constexpr std::size_t kRecords = 0xD8;
constexpr std::size_t kHoleCount = 0x114;
constexpr std::size_t kHoleOffset = 0x118;
constexpr std::size_t kHoles = 0x14C;
constexpr std::size_t kHolesDirty = 0x158;
constexpr std::size_t kTypeCounts = 0x15C;
constexpr std::size_t kGroupCounts = 0x170;
constexpr std::size_t kCompressTypes = 0x188;
constexpr std::size_t kCompressGroups = 0x19C;
constexpr std::size_t kDirectory = 0x1B0;
} // namespace object

namespace vmain { // the main vtable (0x00AD87D0), by offset
constexpr std::size_t kGetIndexedSize = 0x48;
constexpr std::size_t kIsCompressed = 0x80;
constexpr std::size_t kOpenForWrite = 0x9C;
constexpr std::size_t kReadSpan = 0xB0;
} // namespace vmain

namespace file { // cRZFile (vtable 0x00ABFED0), by offset
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSeek = 0x30;
constexpr std::size_t kRead = 0x38;
} // namespace file

namespace lock {
constexpr std::size_t kUnlock = 0x08;
} // namespace lock

namespace record {
constexpr std::size_t kInterface = 0x04;
constexpr std::size_t kFlags = 0x0A;
constexpr std::size_t kKind = 0x40;
constexpr std::size_t kData = 0x44; // kind 1: offset in the file; kind 2: the shared buffer
constexpr std::size_t kSize = 0x4C;
constexpr std::size_t kAddRef = 0x04;       // both vtables
constexpr std::size_t kGetFlags = 0x10;     // the interface's vtable
constexpr std::size_t kGetSize = 0xD0;      // the main vtable
} // namespace record

namespace index_node {
constexpr std::size_t kOffset = 0x10;
constexpr std::size_t kSize = 0x14;
} // namespace index_node

constexpr std::size_t kDirectoryNodeSize = 0x10;
constexpr std::size_t kMapNodeRecord = 0x1C;
constexpr uint32_t kHeaderSize = 0x60;

struct Key {
    uint32_t type, group, instance;
};
static_assert(sizeof(Key) == 0xC);

// A hash table iterator as the game's helpers write it.
struct HashIterator {
    char* node;
    void* table;
};
static_assert(sizeof(HashIterator) == 8);

// The scope guard 0x0048C9F0 builds: its vtable and the lock it took.
struct LockGuard {
    void* vtable;
    void* held;
};
static_assert(sizeof(LockGuard) == 8);

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

void TakeLock(LockGuard& guard, char* self)
{
    This<void*>(addr::SC4LockGuard_Construct, &guard, static_cast<void*>(self + object::kLock));
}

void Unlock(LockGuard& guard)
{
    Virtual<uint32_t>(guard.held, lock::kUnlock);
}

// The index entry for `key` (0x0096EEA9 on a copy of the key, as every
// caller here makes one), or NULL.
char* FindInIndex(char* self, const Key* key)
{
    Key copy;
    copy.type = key->type;
    copy.group = key->group;
    copy.instance = key->instance;
    return This<char*>(addr::cGZDBSegmentPackedFile_IndexFind, At<void*>(self, object::kIndex), &copy);
}

// The directory's node for `key` (0x0096F290), NULL when the record is not
// compressed.
char* FindInDirectory(char* self, Key* key)
{
    HashIterator found;
    This<void*>(addr::cGZDBSegmentPackedFile_HashFind, self + object::kDirectory, &found, key);
    return found.node;
}

// The node of the open-record map for `key`, NULL when the key has none.
char* FindOpenRecord(char* self, Key* key)
{
    char* const map = self + object::kRecords;
    char* const node = This<char*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, key);
    return node == At<char*>(map, 0) ? nullptr : node;
}

// The game's 0x0096FC95: true when every index entry lies inside the file,
// after the header. `self` is the object.
uint8_t __fastcall ValidateIndex(char* self, void*)
{
    const uint32_t length = Virtual<uint32_t>(self + object::kFile, file::kLength);
    HashIterator at;
    This<void>(addr::cGZDBSegmentPackedFile_KeyTableBegin, At<void*>(self, object::kIndex), &at);
    while (at.node != nullptr) {
        const uint32_t offset = At<uint32_t>(at.node, index_node::kOffset);
        if (offset < kHeaderSize || offset >= length)
            return 0;
        if (At<uint32_t>(at.node, index_node::kSize) + offset > length)
            return 0;
        This<void*>(addr::cGZDBSegmentPackedFile_HashIteratorNext, &at);
    }
    return 1;
}

// The game's 0x0096FCE8: reads the holes table the header names into the
// holes map and has it checked (0x0096E49D). True when there are no holes or
// all of that worked. `self` is the object.
uint8_t __fastcall ReadHoles(char* self, void*)
{
    if (At<uint32_t>(self, object::kHoleCount) == 0)
        return 1;
    uint8_t result = 0;
    char* const stream = self + object::kFile;
    const uint32_t reached = Virtual<uint32_t>(
        stream, file::kSeek, At<uint32_t>(self, object::kBaseOffset) + At<uint32_t>(self, object::kHoleOffset));
    if (reached != At<uint32_t>(self, object::kBaseOffset) + At<uint32_t>(self, object::kHoleOffset))
        return 0;
    const uint32_t count = At<uint32_t>(self, object::kHoleCount);
    char* const table = Cdecl<char*>(addr::SC4_operator_new, count << 3);
    if (table != nullptr) {
        // The array constructor loop; the element constructor does nothing.
        reinterpret_cast<void(__stdcall*)(void*, uint32_t, int32_t, void*)>(addr::SC4UI_F41D490)(
            table, uint32_t{8}, static_cast<int32_t>(count),
            reinterpret_cast<void*>(addr::cGZDBSegmentPackedFile_ConstructNothing));
    }
    if (Virtual<uint8_t>(stream, file::kRead, table, At<uint32_t>(self, object::kHoleCount) << 3)) {
        // (The original first counts up to the hole count in an empty loop.)
        if (At<uint32_t>(self, object::kHoleCount) > 0) {
            char* hole = table;
            uint32_t i = 0;
            do {
                uint32_t inserted[2]; // {iterator, a byte}
                This<void*>(addr::cGZDBSegmentPackedFile_HoleInsert, self + object::kHoles, &inserted[0], hole);
                hole += 8;
                ++i;
            } while (i < At<uint32_t>(self, object::kHoleCount));
        }
        if (This<uint8_t>(addr::cGZDBSegmentPackedFile_ValidateHoles, self)) {
            At<uint8_t>(self, object::kHolesDirty) = 0;
            result = 1;
        }
    }
    Cdecl<void>(addr::SC4_operator_delete, table);
    return result;
}

// The game's 0x0096FF7E: reads the compressed record `key` and decompresses
// it through 0x0096E6F4, which takes the buffer through `*buffer` and gives
// the size through `size`. False when the record is not in the directory or
// not in the index. `self` is the object.
uint8_t __fastcall ReadCompressed(char* self, void*, Key* key, void** buffer, uint32_t* size)
{
    if (FindInDirectory(self, key) == nullptr)
        return 0;
    char* const entry = FindInIndex(self, key);
    if (entry == nullptr)
        return 0;
    return This<uint8_t>(addr::cGZDBSegmentPackedFile_ReadCompressedAt, self,
                         At<uint32_t>(entry, index_node::kOffset), At<uint32_t>(entry, index_node::kSize), buffer,
                         size);
}

// The game's 0x0096FFE3, main slot 34: under the lock, what main slot +0x48
// says of a record that is in the directory, else -1. `self` is the object.
uint32_t __fastcall GetCompressedRecordSize(char* self, void*, Key* key)
{
    LockGuard guard;
    TakeLock(guard, self);
    uint32_t result = 0xFFFFFFFF;
    if (FindInDirectory(self, key) != nullptr)
        result = Virtual<uint32_t>(self, vmain::kGetIndexedSize, key);
    Unlock(guard);
    return result;
}

// The game's 0x00970030: the uncompressed size the directory has for `key`,
// or -1. `self` is the object.
uint32_t __fastcall DirectorySize(char* self, void*, Key* key)
{
    char* const node = FindInDirectory(self, key);
    return node != nullptr ? At<uint32_t>(node, kDirectoryNodeSize) : 0xFFFFFFFF;
}

// The game's 0x0097005C: makes room for one more entry in a key hash table
// (0x0096F614), then inserts `value` unless its key is there (0x0096F209).
// `self` is the table; the result {node, inserted} goes to `result`.
void* __fastcall InsertIntoIndex(char* self, void*, void* result, void* value)
{
    This<void>(addr::cGZDBSegmentPackedFile_HashResize, self, At<uint32_t>(self, 0x10) + 1);
    This<void*>(addr::cGZDBSegmentPackedFile_IndexInsertNoResize, self, result, value);
    return result;
}

// The game's 0x009700E5: the destructor. `self` is the object. It ends in
// the base class's destructor (0x0097569C on the interface), whose result it
// passes on.
uint32_t __fastcall Destruct(char* self, void*)
{
    char* const iface = self + object::kInterface;
    At<uint32_t>(self, 0) = static_cast<uint32_t>(addr::cGZDBSegmentPackedFile_vftable);
    At<uint32_t>(iface, 0) = static_cast<uint32_t>(addr::cGZDBSegmentPackedFile_interfaceVftable);
    This<uint32_t>(addr::cGZDBSegmentPackedFile_Shutdown, self);
    This<void>(addr::cGZDBSegmentPackedFile_KeyTableDestroy, self + object::kDirectory);
    This<void>(addr::SC4Uint32Hash_Destroy, self + object::kCompressGroups);
    This<void>(addr::SC4Uint32Hash_Destroy, self + object::kCompressTypes);
    This<void>(addr::SC4Uint32Hash_Destroy, self + object::kGroupCounts);
    This<void>(addr::SC4Uint32Hash_Destroy, self + object::kTypeCounts);
    This<void>(addr::cGZDBSegmentPackedFile_DestroyHoleTree, self + object::kHoles);
    This<void>(addr::cGZDBSegmentPackedFile_DestroyOpenTree, self + object::kRecords);
    This<void>(addr::cRZFile_Destruct, self + object::kFile);
    This<void>(addr::SC4String_Destroy, self + object::kPath);
    return This<uint32_t>(addr::cGZPersistDBSegment_Destruct, iface);
}

// The game's 0x009710D6: the deleting destructor (interface slot 30 comes
// here through the thunk 0x0097018B). `self` is the object.
void* __fastcall DeleteSegment(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (static_cast<uint8_t>(flags) & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// resources.stale_record_fix.
bool g_staleRecordFix = false;

bool IsStale(void* opened)
{
    return opened && At<uint32_t>(opened, record::kKind) == 2 && !At<void*>(opened, record::kData) &&
           At<uint16_t>(opened, record::kFlags) == 0;
}

// `segment` is the object.
void EraseStale(char* segment, const Key* key)
{
    void* map = segment + object::kRecords;
    for (;;) {
        void* node = This<void*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, key);
        if (node == At<void*>(map, 0)) // the header: no node for the key
            return;
        void* opened = At<void*>(node, kMapNodeRecord);
        if (!IsStale(opened))
            return;
        log::Write("stale_record_fix: dropped a closed record from the open map: %08X-%08X-%08X (record %p)",
                   key->type, key->group, key->instance, opened);
        This<void>(addr::cGZDBSegmentPackedFile_RecordMapErase, map, node);
    }
}

__declspec(noinline) void GuardedEraseStale(char* segment, const Key* key)
{
    __try {
        EraseStale(segment, key);
    } __except (crash::HookFilter("resources.stale_record_fix", GetExceptionInformation())) {
    }
}

void* AllocateRecord(std::uintptr_t pool)
{
    return This<void*>(addr::SC4Pool_Allocate, reinterpret_cast<void*>(pool));
}

// The read branch of the open: the new record's interface pointer with the
// count the caller gets, or NULL. `self` is the object.
char* OpenForRead(char* self, Key* key)
{
    char* const map = self + object::kRecords;
    char* made = nullptr;
    char* const node = This<char*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, key);
    if (node != At<char*>(map, 0)) {
        // The key is open already: a second record on the first one's data.
        char* const opened = At<char*>(node, kMapNodeRecord);
        if (Virtual<uint32_t>(opened + record::kInterface, record::kGetFlags) & 2)
            return nullptr; // open for writing
        const uint32_t kind = At<uint32_t>(opened, record::kKind);
        if (kind == 2) {
            if (void* slot = AllocateRecord(addr::cGZDBSegmentPackedFile_MemoryRecordPool)) {
                made = This<char*>(addr::cGZDBPackedMemoryRecord_ConstructShared, slot,
                                   At<void*>(opened, record::kData), At<uint32_t>(opened, record::kSize), key,
                                   static_cast<void*>(self), int32_t{1});
            }
        } else if (kind == 1) {
            if (void* slot = AllocateRecord(addr::cGZDBSegmentPackedFile_FileRecordPool)) {
                made = This<char*>(addr::cGZDBPackedFileRecord_Construct, slot, At<uint32_t>(opened, record::kData),
                                   At<uint32_t>(opened, record::kSize), key, static_cast<void*>(self), int32_t{1});
            }
        } else {
            return nullptr;
        }
    } else {
        char* const entry = FindInIndex(self, key);
        if (entry == nullptr)
            return nullptr;
        if (FindInDirectory(self, key) != nullptr) {
            // Compressed: read and decompressed whole, whatever its size.
            void* data = nullptr;
            uint32_t size = 0;
            if (!This<uint8_t>(addr::cGZDBSegmentPackedFile_ReadCompressedAt, self,
                               At<uint32_t>(entry, index_node::kOffset), At<uint32_t>(entry, index_node::kSize), &data,
                               &size))
                return nullptr;
            if (void* slot = AllocateRecord(addr::cGZDBSegmentPackedFile_MemoryRecordPool)) {
                made = This<char*>(addr::cGZDBPackedMemoryRecord_Construct, slot, data, size, key,
                                   static_cast<void*>(self), int32_t{1});
            }
        } else if (At<uint32_t>(entry, index_node::kSize) > At<uint32_t>(self, object::kMemoryLimit)) {
            // Large: a record that reads from the file as it is read.
            if (void* slot = AllocateRecord(addr::cGZDBSegmentPackedFile_FileRecordPool)) {
                made = This<char*>(addr::cGZDBPackedFileRecord_Construct, slot,
                                   At<uint32_t>(entry, index_node::kOffset), At<uint32_t>(entry, index_node::kSize),
                                   key, static_cast<void*>(self), int32_t{1});
            }
        } else {
            // Small: read into memory now.
            void* const data = Cdecl<void*>(addr::SC4_operator_new, At<uint32_t>(entry, index_node::kSize));
            if (!Virtual<uint8_t>(self, vmain::kReadSpan, data, At<uint32_t>(entry, index_node::kOffset),
                                  At<uint32_t>(entry, index_node::kSize))) {
                Cdecl<void>(addr::SC4_operator_delete, data);
                return nullptr;
            }
            if (void* slot = AllocateRecord(addr::cGZDBSegmentPackedFile_MemoryRecordPool)) {
                made = This<char*>(addr::cGZDBPackedMemoryRecord_Construct, slot, data,
                                   At<uint32_t>(entry, index_node::kSize), key, static_cast<void*>(self),
                                   int32_t{1});
            }
        }
    }
    // The count for the caller. Like the original, this does not check for a
    // record the pool could not give (the pool never answers NULL).
    char* const result = made != nullptr ? made + record::kInterface : nullptr;
    Virtual<uint32_t>(result, record::kAddRef);
    if (result == nullptr)
        return nullptr;
    // The count for the open-record map, then the map's entry.
    struct {
        Key key;
        char* opened;
    } value;
    value.key.type = key->type;
    value.key.group = key->group;
    value.key.instance = key->instance;
    value.opened = result - record::kInterface;
    Virtual<uint32_t>(value.opened, record::kAddRef);
    void* where[2];
    This<void*>(addr::cGZDBSegmentPackedFile_OpenInsert, map, &where[0], &value);
    return result;
}

// The game's 0x009701FD, interface slot 34: OpenRecord without the lock.
// `self` is the interface (object + 4). A write-open goes to main slot +0x9C
// and its answer is passed on untouched (`*out` is that call's business); a
// read-open gives the record's interface in `*out` and 1, or NULL and 0.
uint32_t __fastcall OpenRecordUnlocked(char* self, void*, Key* key, void** out, uint32_t access)
{
    char* const segment = self - object::kInterface;
    if (g_staleRecordFix && (access & 3) == 1 && (At<uint8_t>(segment, object::kOpenFlags) & 3) && key)
        GuardedEraseStale(segment, key);
    char* opened = nullptr;
    if (At<uint8_t>(segment, object::kOpenFlags) & 3) {
        if (access & 2)
            return Virtual<uint32_t>(segment, vmain::kOpenForWrite, key, out);
        if (access & 1)
            opened = OpenForRead(segment, key);
    }
    *out = opened;
    return opened != nullptr ? 1 : 0;
}

// The game's 0x0097042E, interface slot 33: GetRecordSize. `self` is the
// interface. A record open for writing answers for itself; else the
// directory's uncompressed size, else the index's size; -1 when the segment
// is closed or the key unknown.
uint32_t __fastcall GetRecordSize(char* self, void*, Key* key)
{
    char* const segment = self - object::kInterface;
    const uint32_t flags = At<uint32_t>(segment, object::kOpenFlags);
    if ((flags & 3) == 0)
        return 0xFFFFFFFF;
    if (flags & 2) {
        if (char* const node = FindOpenRecord(segment, key)) {
            const uint32_t size = Virtual<uint32_t>(At<void*>(node, kMapNodeRecord), record::kGetSize);
            if (size != 0xFFFFFFFF)
                return size;
        }
    }
    if (Virtual<uint8_t>(segment, vmain::kIsCompressed, key))
        return DirectorySize(segment, nullptr, key);
    char* const entry = FindInIndex(segment, key);
    if (entry == nullptr)
        return 0xFFFFFFFF;
    return At<uint32_t>(entry, index_node::kSize);
}

// Seeks to an index entry's data and reads `count` bytes of it; false when
// the seek lands elsewhere or the read fails. `self` is the object.
bool ReadAt(char* self, char* entry, void* buffer, uint32_t count)
{
    char* const stream = self + object::kFile;
    const uint32_t reached = Virtual<uint32_t>(
        stream, file::kSeek, At<uint32_t>(self, object::kBaseOffset) + At<uint32_t>(entry, index_node::kOffset));
    if (reached != At<uint32_t>(self, object::kBaseOffset) + At<uint32_t>(entry, index_node::kOffset))
        return false;
    return Virtual<uint8_t>(stream, file::kRead, buffer, count) != 0;
}

// The game's 0x009704BD, interface slot 39: ReadRecord. `self` is the
// interface. With a buffer: reads up to `*size` bytes of the record
// (decompressed when it is compressed), leaves the count read in `*size` and
// returns it. Without: `*size` = 0 and the record's (uncompressed) size is
// returned. A record open for writing: `*size` = 0, -1. Closed segment,
// write-only segment or unknown key: `*size` = 0, -1.
uint32_t __fastcall ReadRecord(char* self, void*, Key* key, void* buffer, uint32_t* size)
{
    char* const segment = self - object::kInterface;
    const uint32_t flags = At<uint32_t>(segment, object::kOpenFlags);
    uint32_t result = 0xFFFFFFFF;
    uint32_t read = 0;
    if ((flags & 3) != 0 && (flags & 1) != 0) {
        if (flags & 2) {
            if (char* const node = FindOpenRecord(segment, key)) {
                char* const opened = At<char*>(node, kMapNodeRecord);
                if (Virtual<uint32_t>(opened + record::kInterface, record::kGetFlags) & 2) {
                    *size = 0;
                    return 0xFFFFFFFF;
                }
            }
        }
        if (char* const entry = FindInIndex(segment, key)) {
            char* const compressed = FindInDirectory(segment, key);
            if (buffer != nullptr) {
                if (compressed != nullptr) {
                    void* target = buffer;
                    if (ReadCompressed(segment, nullptr, key, &target, size))
                        read = *size;
                } else {
                    const uint32_t recordSize = At<uint32_t>(entry, index_node::kSize);
                    const uint32_t wanted = *size;
                    const uint32_t count = recordSize < wanted ? recordSize : wanted;
                    if (ReadAt(segment, entry, buffer, count))
                        read = count;
                }
                *size = read;
                result = read;
            } else {
                *size = 0;
                if (compressed != nullptr)
                    result = DirectorySize(segment, nullptr, key);
                else
                    result = At<uint32_t>(entry, index_node::kSize);
            }
        }
    }
    *size = read;
    return result;
}

// The game's 0x0097078F, main slot 19: the same read under the lock and
// without decompression: the record's bytes as they are in the file. `self`
// is the object. With a buffer: up to `*size` bytes, the count read in
// `*size` and returned (0 when the seek or the read fails). Without:
// `*size` = 0 and the size is returned, the uncompressed size for a
// compressed record. Failures as ReadRecord, except that `*size` is left
// alone when the segment is closed or the key unknown.
uint32_t __fastcall ReadRecordRaw(char* self, void*, Key* key, void* buffer, uint32_t* size)
{
    LockGuard guard;
    TakeLock(guard, self);
    const uint32_t flags = At<uint32_t>(self, object::kOpenFlags);
    uint32_t result = 0xFFFFFFFF;
    if ((flags & 3) != 0 && (flags & 1) != 0) {
        if (flags & 2) {
            if (char* const node = FindOpenRecord(self, key)) {
                char* const opened = At<char*>(node, kMapNodeRecord);
                if (Virtual<uint32_t>(opened + record::kInterface, record::kGetFlags) & 2) {
                    *size = 0;
                    Unlock(guard);
                    return 0xFFFFFFFF;
                }
            }
        }
        if (char* const entry = FindInIndex(self, key)) {
            if (buffer != nullptr) {
                const uint32_t recordSize = At<uint32_t>(entry, index_node::kSize);
                const uint32_t wanted = *size;
                const uint32_t count = recordSize < wanted ? recordSize : wanted;
                result = ReadAt(self, entry, buffer, count) ? count : 0;
                *size = result;
            } else {
                char* const compressed = FindInDirectory(self, key);
                *size = 0;
                if (compressed != nullptr)
                    result = DirectorySize(self, nullptr, key);
                else
                    result = At<uint32_t>(entry, index_node::kSize);
            }
        }
    }
    Unlock(guard);
    return result;
}

// The one detour on OpenRecordUnlocked, shared by the part and the fix.
bool InstallOpenRecord()
{
    static bool installed = false;
    static void* original = nullptr;
    if (installed)
        return true;
    if (!game::IsBuild641())
        return false;
    const addr::Entry* found = addr_check::Find(addr::cGZDBSegmentPackedFile_OpenRecordUnlocked);
    installed = found && hook::Install(*found, reinterpret_cast<void*>(&OpenRecordUnlocked), &original);
    return installed;
}

bool InstallPackedFile3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[11] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZDBSegmentPackedFile_ValidateIndex, reinterpret_cast<void*>(&ValidateIndex)},
        {addr::cGZDBSegmentPackedFile_ReadHoles, reinterpret_cast<void*>(&ReadHoles)},
        {addr::cGZDBSegmentPackedFile_ReadCompressed, reinterpret_cast<void*>(&ReadCompressed)},
        {addr::cGZDBSegmentPackedFile_GetCompressedRecordSize, reinterpret_cast<void*>(&GetCompressedRecordSize)},
        {addr::cGZDBSegmentPackedFile_DirectorySize, reinterpret_cast<void*>(&DirectorySize)},
        {addr::cGZDBSegmentPackedFile_InsertIntoIndex, reinterpret_cast<void*>(&InsertIntoIndex)},
        {addr::cGZDBSegmentPackedFile_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cGZDBSegmentPackedFile_GetRecordSize, reinterpret_cast<void*>(&GetRecordSize)},
        {addr::cGZDBSegmentPackedFile_ReadRecord, reinterpret_cast<void*>(&ReadRecord)},
        {addr::cGZDBSegmentPackedFile_ReadRecordRaw, reinterpret_cast<void*>(&ReadRecordRaw)},
        {addr::cGZDBSegmentPackedFile_DeleteSegment, reinterpret_cast<void*>(&DeleteSegment)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return InstallOpenRecord();
}

bool InstallStaleRecordFix(registry::Mode)
{
    g_staleRecordFix = true;
    return InstallOpenRecord();
}

const bool g_packedFile3Registered = registry::Register({
    "resources.packed_file.part3",
    "resources",
    registry::Kind::Detour,
    registry::Phase::PreAppInit,
    addr::cGZDBSegmentPackedFile_OpenRecordUnlocked,
    "Our packed file, part 3: index and holes checks, the destructor, opening a record for reading, record sizes and "
    "reads",
    false,
    InstallPackedFile3,
});

const bool g_staleRecordFixRegistered = registry::Register({
    "resources.stale_record_fix",
    "resources",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZDBSegmentPackedFile_OpenRecordUnlocked,
    "Fixes the game's crash at 0x0096DA20: a read-open skips a closed record left in the segment's open map",
    false,
    InstallStaleRecordFix,
    true,
    true, // a fix of the original's bug: always on (registry.h)
});

} // namespace
} // namespace opensc4::packed_file
