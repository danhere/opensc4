// The framework's packed file, cGZDBSegmentPackedFile (GZCOM class
// 0x086D4A89, docs/resources.md), read side, part 4: 0x009710F2 to
// 0x00972B73 and 0x00975616 to 0x009759C5. Replacement, off by default:
//
//   resources.packed_file.part4: Close, the per-type and per-group record
//     counts, reading the directory of compressed records, CloseRecord, the
//     constructor, the scan for a package embedded in another file, reading
//     the index, the static probe of a plugin file, the header reader, Open,
//     and the base class's reference counting and locked public calls.
//
// The object (0x1C8 bytes) as this part uses it, offsets from its start (the
// interface functions get the object + 4):
//
//   +0x00  main vtable 0x00AD87D0 (0x00AD8488 while the base is built)
//   +0x04  the cIGZPersistDBSegment interface, vtable 0x00AD8728
//   +0x08  reference count
//   +0x0C  the lock (slot +4 locks, slot +8 unlocks)
//   +0x14  initialised byte
//   +0x18  segment ID
//   +0x1C  open flags: bit 0 read, bit 1 write
//   +0x24  the index: a heap hash table of 0x14 bytes, key to {offset, size}
//   +0x28  index dirty byte
//   +0x2C  the path (game string: +4 begin, +8 end)
//   +0x40  the file, an embedded cRZFile (0x80 bytes)
//   +0xC0  where the package starts in the file; +0xC4, +0xC8 = -1
//   +0xCC, +0xD0  user version; +0xD4 = 0x200
//   +0xD8  the open-record multimap {header, count}; node +0x10 key,
//          +0x1C record
//   +0xE4  the 0x60-byte header as read (+0x108 index count, +0x10C index
//          offset)
//   +0x144 = 3, +0x148 = 0.15f at construction
//   +0x14C the holes map; +0x158 holes dirty, +0x159 compacting
//   +0x15A key tracking on; +0x15C records per type, +0x170 records per
//          group (hash tables from a dword to a count: node +4 key, +8 count)
//   +0x184 directory dirty, +0x185 compress everything (1 at construction)
//   +0x188, +0x19C the sets of types and groups to compress
//   +0x1B0 the directory: key to uncompressed size
//   +0x1C4 a scratch buffer pointer
//
// Every file call goes through the embedded file's vtable, as in the
// original. The header reader's call to the embedded-package scan goes to
// whatever the game's call instruction at 0x009729E1 targets at that moment
// (a mod rewrites it).
//
// Still the game's in these ranges: the write side (0x00971178, 0x009712D7,
// 0x009714FF, 0x00971543, 0x00971587, 0x00971617, 0x00971A55 to 0x00971FE2,
// 0x00972233, 0x00972542), the thunks 0x00971981 and 0x00971989, the small
// classes at 0x00975616 to 0x0097569C, 0x0097598D, 0x009759A0, and the
// locked AddRef 0x009756CF, which is folded with unrelated classes.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::packed_file {
namespace {

namespace object { // offsets from the object's start
constexpr std::size_t kInterface = 0x04;
constexpr std::size_t kLock = 0x0C;
constexpr std::size_t kInitialised = 0x14;
constexpr std::size_t kSegmentId = 0x18;
constexpr std::size_t kFlags = 0x1C;
constexpr std::size_t kUnknown20 = 0x20;
constexpr std::size_t kIndex = 0x24;
constexpr std::size_t kIndexDirty = 0x28;
constexpr std::size_t kPath = 0x2C;
constexpr std::size_t kFile = 0x40;
constexpr std::size_t kBase = 0xC0;
constexpr std::size_t kRecords = 0xD8;
constexpr std::size_t kHeader = 0xE4;
constexpr std::size_t kIndexCount = 0x108;
constexpr std::size_t kIndexOffset = 0x10C;
constexpr std::size_t kHoles = 0x14C;
constexpr std::size_t kTracking = 0x15A;
constexpr std::size_t kTypeCounts = 0x15C;
constexpr std::size_t kGroupCounts = 0x170;
constexpr std::size_t kDirectory = 0x1B0;
} // namespace object

namespace iface { // offsets from the interface (object + 4)
constexpr std::size_t kCount = 0x04;
constexpr std::size_t kLock = 0x08;
constexpr std::size_t kInitialised = 0x10;
constexpr std::size_t kFlags = 0x18;
constexpr std::size_t kIndex = 0x20;
constexpr std::size_t kPath = 0x28;
constexpr std::size_t kPathBegin = 0x2C;
constexpr std::size_t kPathEnd = 0x30;
constexpr std::size_t kFile = 0x3C;
constexpr std::size_t kRecords = 0xD4;
} // namespace iface

namespace slot { // the interface's vtable (0x00AD8728), by offset
constexpr std::size_t kAddRef = 0x04;
constexpr std::size_t kFlush = 0x24;
constexpr std::size_t kCountRecords = 0x48;
constexpr std::size_t kCloseRecord = 0x60;
constexpr std::size_t kAbortRecord = 0x68;
constexpr std::size_t kDeletingDestructor = 0x78;
constexpr std::size_t kDeleteSelf = 0x7C;
constexpr std::size_t kTestForRecord = 0x80;
constexpr std::size_t kGetRecordSize = 0x84;
constexpr std::size_t kOpenRecord = 0x88;
constexpr std::size_t kCreateNewRecord = 0x8C;
constexpr std::size_t kCloseRecordUnlocked = 0x90;
constexpr std::size_t kAbortRecordUnlocked = 0x94;
constexpr std::size_t kDeleteRecord = 0x98;
constexpr std::size_t kReadRecord = 0x9C;
constexpr std::size_t kWriteRecord = 0xA0;
} // namespace slot

namespace file { // cRZFile's vtable (0x00ABFED0), by offset
constexpr std::size_t kOpen = 0x0C;
constexpr std::size_t kIsOpen = 0x10;
constexpr std::size_t kClose = 0x14;
constexpr std::size_t kPosition = 0x18;
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSeekToBegin = 0x24;
constexpr std::size_t kSeekRelative = 0x2C;
constexpr std::size_t kSeek = 0x30;
constexpr std::size_t kRead = 0x38;
constexpr std::size_t kReadWithCount = 0x3C;
constexpr std::size_t kSetPath = 0x70;
} // namespace file

namespace lock {
constexpr std::size_t kLock = 0x04;
constexpr std::size_t kUnlock = 0x08;
} // namespace lock

// The directory of compressed records is itself a record.
constexpr uint32_t kDirectoryType = 0xE86B1EEF;
constexpr uint32_t kDirectoryGroup = 0xE86B1EEF;
constexpr uint32_t kDirectoryInstance = 0x286B1F03;

constexpr uint32_t kSegmentInterface = 0x656B8F0C; // cIGZPersistDBSegment

// The header reader's call to the embedded-package scan.
constexpr std::uintptr_t kScanCallSite = 0x009729E1;

struct Key {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

struct IndexEntry { // as in the file and as the index table takes it
    Key key;
    uint32_t offset;
    uint32_t size;
};
static_assert(sizeof(IndexEntry) == 0x14);

struct DirectoryEntry { // as in the directory record
    Key key;
    uint32_t size;
};
static_assert(sizeof(DirectoryEntry) == 0x10);

struct Bytes { // the game's byte string body
    char* begin;
    char* end;
    char* capacity;
};

struct LockGuard { // what 0x0048C9F0 builds
    void* vtable;
    void* lock;
};

struct TableIterator { // an iterator of the index table
    char* node;
    void* table;
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

void Lock(LockGuard& guard, void* lockObject)
{
    This<void*>(addr::SC4LockGuard_Construct, &guard, lockObject);
}

uint32_t Unlock(LockGuard& guard)
{
    return Virtual<uint32_t>(guard.lock, lock::kUnlock);
}

// A new, empty index table (0x14 bytes), as the original makes it in three
// places.
void* NewIndexTable()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    if (!memory)
        return nullptr;
    return This<void*>(addr::cGZDBSegmentPackedFile_KeyTableConstruct, memory);
}

// The game's 0x0097139C: one record more (`add`) or one less of a type and a
// group, when key tracking is on. A count that would fall to zero leaves the
// table. RET 0xC.
void __fastcall UpdateKeyCounts(char* self, void*, uint32_t type, uint32_t group, uint32_t add)
{
    if (At<uint8_t>(self, object::kTracking) == 0)
        return;
    uint32_t inserted[4];
    char* const types = self + object::kTypeCounts;
    if (char* const node = This<char*>(addr::SC4Uint32HashSet_Find, types, &type)) {
        if (static_cast<uint8_t>(add) != 0) {
            ++At<int32_t>(node, 8);
        } else {
            const int32_t count = At<int32_t>(node, 8);
            if (count > 1)
                At<int32_t>(node, 8) = count - 1;
            else
                This<void>(addr::SC4Uint32HashSet_EraseAt, types, node, types);
        }
    } else if (static_cast<uint8_t>(add) != 0) {
        uint32_t entry[2] = {type, 1};
        This<void*>(addr::SC4Uint32HashSet_InsertPair, types, &inserted[0], &entry[0]);
    }
    if (At<uint8_t>(self, object::kTracking) == 0)
        return;
    char* const groups = self + object::kGroupCounts;
    if (char* const node = This<char*>(addr::SC4Uint32HashSet_Find, groups, &group)) {
        if (static_cast<uint8_t>(add) != 0) {
            ++At<int32_t>(node, 8);
        } else {
            const int32_t count = At<int32_t>(node, 8);
            if (count > 1)
                At<int32_t>(node, 8) = count - 1;
            else
                This<void>(addr::SC4Uint32HashSet_EraseAt, groups, node, groups);
        }
    } else if (static_cast<uint8_t>(add) != 0) {
        uint32_t entry[2] = {group, 1};
        This<void*>(addr::SC4Uint32HashSet_InsertPair, groups, &inserted[0], &entry[0]);
    }
}

// The game's 0x00971481, slot 24 of the main vtable: turns the per-type and
// per-group counts on or off under the lock; turning them on counts the
// whole index. The original has no check for a missing index. What it leaves
// in EAX is the unlock's result. RET 4.
uint32_t __fastcall SetKeyTracking(char* self, void*, uint32_t on)
{
    LockGuard guard;
    Lock(guard, self + object::kLock);
    if (At<uint8_t>(self, object::kTracking) != static_cast<uint8_t>(on)) {
        At<uint8_t>(self, object::kTracking) = static_cast<uint8_t>(on);
        This<void>(addr::SC4Uint32HashSet_Clear, self + object::kTypeCounts);
        This<void>(addr::SC4Uint32HashSet_Clear, self + object::kGroupCounts);
        if (At<uint8_t>(self, object::kTracking) != 0) {
            TableIterator at;
            This<void>(addr::cGZDBSegmentPackedFile_KeyTableBegin, At<void*>(self, object::kIndex), &at);
            while (at.node) {
                UpdateKeyCounts(self, nullptr, At<uint32_t>(at.node, 4), At<uint32_t>(at.node, 8), 1);
                This<void*>(addr::cGZDBSegmentPackedFile_F96F5B7, &at);
            }
        }
    }
    return Unlock(guard);
}

// The game's 0x009718B9: empties the directory and fills it from the
// directory record, 16 bytes per compressed record. No such record is a
// success. The record is read through the interface's unlocked ReadRecord,
// first for its size, then into a buffer of the size + 4.
uint8_t __fastcall ReadDirectory(char* self, void*)
{
    char* const directory = self + object::kDirectory;
    uint8_t ok = 0;
    This<void>(addr::cGZDBSegmentPackedFile_KeyTableClear, directory);
    uint32_t size = 0;
    Key key = {kDirectoryType, kDirectoryGroup, kDirectoryInstance};
    char* const segment = self + object::kInterface;
    const uint32_t found = Virtual<uint32_t>(segment, slot::kReadRecord, &key, static_cast<void*>(nullptr), &size);
    size = found;
    if (found == 0xFFFFFFFF)
        return 1;
    char* const buffer = Cdecl<char*>(addr::SC4_operator_new, found + 4);
    if (Virtual<uint32_t>(segment, slot::kReadRecord, &key, buffer, &size) != 0) {
        uint32_t left = size >> 4;
        const DirectoryEntry* from = reinterpret_cast<const DirectoryEntry*>(buffer);
        for (; left != 0; --left, ++from) {
            DirectoryEntry entry;
            entry.key.type = from->key.type;
            entry.key.group = from->key.group;
            entry.key.instance = from->key.instance;
            entry.size = from->size;
            uint32_t inserted[4];
            This<void*>(addr::cGZDBSegmentPackedFile_DirectoryInsert, directory, &inserted[0], &entry);
        }
        ok = 1;
    }
    Cdecl<void>(addr::SC4_operator_delete, buffer);
    return ok;
}

// The game's 0x00971991, slot 36 of the interface (CloseRecord, unlocked).
// `self` is the interface, `record` a record's interface (record + 4). Finds
// the record among the open ones with its key; a written record is finished
// first (0x00971617: its data goes into the file and the index); then the
// record is closed, released and taken out of the map. RET 4.
uint8_t __fastcall CloseRecord(char* self, void*, void* record)
{
    if ((At<uint8_t>(self, iface::kFlags) & 3) == 0)
        return 0;
    Key key;
    Virtual<void>(record, 0x0C, &key);
    char* const map = self + iface::kRecords;
    char* node = This<char*>(addr::cGZDBSegmentPackedFile_RecordMapFind, map, &key);
    if (node == At<char*>(map, 0))
        return 0;
    for (;;) {
        if (static_cast<uint8_t>(This<uint32_t>(addr::SC4ResourceKey_Equals, node + 0x10, &key)) == 0)
            return 0;
        char* const open = At<char*>(node, 0x1C);
        if ((open ? open + 4 : nullptr) == record) {
            if ((Virtual<uint32_t>(record, 0x10) & 2) != 0)
                This<uint8_t>(addr::cGZDBSegmentPackedFile_F971617, self - 4, open, &key);
            This<void>(addr::cGZDBSegmentPackedFile_F96E363, self - 4, open ? open + 4 : nullptr);
            Virtual<uint32_t>(open, 0x08);
            This<void>(addr::cGZDBSegmentPackedFile_RecordMapErase, map, node);
            return 1;
        }
        node = Cdecl<char*>(addr::cSC4Advisor_F40D4C0, node);
        if (node == At<char*>(map, 0))
            return 0;
    }
}

// The game's 0x009710F2, slot 8 of the interface (Close). `self` is the
// interface. A segment open for writing is written out first (0x0096FA6B,
// then Flush); whatever that gives, the index, the counts, the holes and the
// directory are dropped and the file is closed. True when nothing was open.
uint8_t __fastcall Close(char* self, void*)
{
    const uint32_t flags = At<uint32_t>(self, iface::kFlags);
    if ((flags & 3) == 0)
        return 1;
    uint8_t ok = 1;
    if ((flags & 2) != 0) {
        ok = This<uint8_t>(addr::cGZDBSegmentPackedFile_F96FA6B, self - 4);
        if (ok != 0)
            ok = Virtual<uint8_t>(self, slot::kFlush);
    }
    if (void* const index = At<void*>(self, iface::kIndex)) {
        This<void>(addr::cGZDBSegmentPackedFile_KeyTableDestroy, index);
        Cdecl<void>(addr::SC4_operator_delete, index);
    }
    At<void*>(self, iface::kIndex) = nullptr;
    char* const start = self - 4;
    This<void>(addr::SC4Uint32HashSet_Clear, start + object::kTypeCounts);
    This<void>(addr::SC4Uint32HashSet_Clear, start + object::kGroupCounts);
    This<void>(addr::cGZDBSegmentPackedFile_F96ED93, start + object::kHoles);
    This<void>(addr::cGZDBSegmentPackedFile_KeyTableClear, start + object::kDirectory);
    Virtual<uint8_t>(self + iface::kFile, file::kClose);
    At<uint32_t>(self, iface::kFlags) = 0;
    return ok;
}

// The game's 0x0097212D: the constructor. `self` is the object.
void* __fastcall Construct(char* self, void*)
{
    At<uint32_t>(self, 0) = static_cast<uint32_t>(addr::cGZDBSegment_vftable);
    This<void*>(addr::cGZPersistDBSegment_Construct, self + object::kInterface);
    At<uint32_t>(self, 0) = static_cast<uint32_t>(addr::cGZDBSegmentPackedFile_vftable);
    At<uint32_t>(self, object::kInterface) = static_cast<uint32_t>(addr::cGZDBSegmentPackedFile_interfaceVftable);
    At<uint8_t>(self, object::kInitialised) = 0;
    At<uint32_t>(self, object::kSegmentId) = 0;
    At<uint32_t>(self, object::kFlags) = 0;
    At<uint32_t>(self, object::kUnknown20) = 0;
    At<uint32_t>(self, object::kIndex) = 0;
    At<uint8_t>(self, object::kIndexDirty) = 0;
    This<void*>(addr::SC4String_SC4String, self + object::kPath);
    This<void*>(addr::cRZFile_ConstructEmpty, self + object::kFile);
    At<uint32_t>(self, 0xC4) = 0xFFFFFFFF;
    At<uint32_t>(self, 0xC8) = 0xFFFFFFFF;
    At<uint32_t>(self, object::kBase) = 0;
    At<uint32_t>(self, 0xCC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    At<uint32_t>(self, 0xD4) = 0x200;
    This<void*>(addr::cGZDBSegmentPackedFile_F96F57B, self + object::kRecords);
    This<void*>(addr::cGZDBSegmentPackedFile_F96D8BC, self + object::kHeader);
    At<uint32_t>(self, 0x144) = 3;
    At<uint32_t>(self, 0x148) = 0x3E19999A; // 0.15f
    This<void*>(addr::cGZDBSegmentPackedFile_HolesMapConstruct, self + object::kHoles);
    At<uint8_t>(self, 0x158) = 0;
    At<uint8_t>(self, 0x159) = 0;
    At<uint8_t>(self, object::kTracking) = 0;
    This<void*>(addr::SC4Uint32HashSet_ConstructDefault, self + object::kTypeCounts);
    This<void*>(addr::SC4Uint32HashSet_ConstructDefault, self + object::kGroupCounts);
    At<uint8_t>(self, 0x184) = 0;
    At<uint8_t>(self, 0x185) = 1;
    This<void*>(addr::SC4Uint32HashSet_ConstructDefault, self + 0x188);
    This<void*>(addr::SC4Uint32HashSet_ConstructDefault, self + 0x19C);
    This<void*>(addr::cGZDBSegmentPackedFile_KeyTableConstruct, self + object::kDirectory);
    At<uint32_t>(self, 0x1C4) = 0;
    return self;
}

// The game's 0x009725FB: looks through the file for a package embedded in
// it. The file is opened for reading when it is not open (and closed again
// after). The file is read in blocks of 0x200 bytes, each block starting 16
// bytes before the end of the last; a block that holds the 16-byte marker
// (0x00AD7BE0 with its first byte inverted) gives a candidate: the file
// offset 16 bytes after the marker's start, where 'DBPF' must stand. The
// answer is the candidate's offset, or -1. As in the original, a candidate
// that fails the 'DBPF' test stays the answer when no later block has a
// marker.
uint32_t __fastcall ScanEmbedded(char* self, void*)
{
    uint32_t found = 0xFFFFFFFF;
    char* const stream = self + object::kFile;
    uint8_t wasOpen = Virtual<uint8_t>(stream, file::kIsOpen);
    if (wasOpen == 0) {
        Virtual<void>(stream, file::kSetPath, self + object::kPath);
        Virtual<uint8_t>(stream, file::kOpen, uint32_t{1}, uint32_t{2}, uint32_t{1});
    }
    if (Virtual<uint8_t>(stream, file::kIsOpen) == 0)
        return found;
    if (Virtual<uint32_t>(stream, file::kLength) > 0x10) {
        // The original passes the address of its `wasOpen` byte as the
        // (empty) allocator argument of both constructors.
        Bytes block;
        This<void*>(addr::SC4ByteString_ConstructFilled, &block, uint32_t{0x200}, uint32_t{0}, &wasOpen);
        Bytes marker;
        const char* const markerBytes = reinterpret_cast<const char*>(addr::cGZDBSegmentPackedFile_kEmbedMarker);
        This<void*>(addr::SC4ByteString_ConstructCopy, &marker, markerBytes, uint32_t{0x10}, &wasOpen);
        char* const pattern = marker.begin;
        *pattern = static_cast<char>(~*pattern);
        uint32_t count = 0x200;
        Virtual<void>(stream, file::kSeekToBegin);
        while (count == 0x200) {
            uint32_t position = Virtual<uint32_t>(stream, file::kPosition);
            count = 0x200;
            if (Virtual<uint8_t>(stream, file::kReadWithCount, block.begin, &count) == 0)
                break;
            This<void>(addr::SC4String_ResizeBody, &block, count, uint32_t{0});
            const uint32_t at = This<uint32_t>(addr::SC4ByteString_Find, &block, pattern, uint32_t{0},
                                               static_cast<uint32_t>(marker.end - pattern));
            if (at != 0xFFFFFFFF) {
                found = at + position + 0x10;
                position = Virtual<uint32_t>(stream, file::kPosition);
                Virtual<uint32_t>(stream, file::kSeek, found);
                uint8_t signature[4] = {0, 0, 0, 0}; // unset in the original
                if (Virtual<uint8_t>(stream, file::kRead, &signature[0], uint32_t{4}) != 0 && signature[0] == 0x44
                    && signature[1] == 0x42 && signature[2] == 0x50 && signature[3] == 0x46)
                    break;
                Virtual<uint32_t>(stream, file::kSeek, position);
            }
            Virtual<uint32_t>(stream, file::kSeekRelative, int32_t{-16});
        }
        Cdecl<void>(addr::SC4_Free, pattern);
        if (block.begin)
            Cdecl<void>(addr::SC4_Free, block.begin);
    }
    if (wasOpen == 0)
        Virtual<uint8_t>(stream, file::kClose);
    return found;
}

// The game's 0x00972761: reads the index (20 bytes per record at the
// header's index offset) into the index table and counts each record's type
// and group, then has the index checked (0x0096FC95). A package without
// records only gets an empty table. The original also runs a constructor
// that does nothing over the entries (0x0096D8B9 through the array helper
// 0x0041D490) and a loop over them without effect; the first is kept.
uint8_t __fastcall ReadIndex(char* self, void*)
{
    uint8_t ok = 0;
    if (At<uint32_t>(self, object::kIndexCount) == 0) {
        ok = 1;
        if (At<void*>(self, object::kIndex) == nullptr)
            At<void*>(self, object::kIndex) = NewIndexTable();
        return ok;
    }
    char* const stream = self + object::kFile;
    const uint32_t indexAt = At<uint32_t>(self, object::kBase) + At<uint32_t>(self, object::kIndexOffset);
    const uint32_t position = Virtual<uint32_t>(stream, file::kSeek, indexAt);
    if (position != At<uint32_t>(self, object::kBase) + At<uint32_t>(self, object::kIndexOffset))
        return ok;
    const uint32_t count = At<uint32_t>(self, object::kIndexCount);
    IndexEntry* const entries = Cdecl<IndexEntry*>(addr::SC4_operator_new, count * 0x14);
    if (entries) {
        using ArrayFn = void(__stdcall*)(void*, uint32_t, uint32_t, std::uintptr_t);
        const std::uintptr_t nothing = addr::cGZDBSegmentPackedFile_F96D8B9;
        reinterpret_cast<ArrayFn>(addr::SC4UI_F41D490)(entries, uint32_t{0x14}, count, nothing);
    }
    if (Virtual<uint8_t>(stream, file::kRead, entries, At<uint32_t>(self, object::kIndexCount) * 0x14) != 0) {
        if (At<void*>(self, object::kIndex) == nullptr)
            At<void*>(self, object::kIndex) = NewIndexTable();
        const IndexEntry* from = entries;
        for (uint32_t i = 0; i < At<uint32_t>(self, object::kIndexCount); ++i, ++from) {
            IndexEntry entry;
            entry.key.type = from->key.type;
            entry.key.group = from->key.group;
            entry.key.instance = from->key.instance;
            entry.offset = from->offset;
            entry.size = from->size;
            uint32_t inserted[4];
            This<void*>(addr::cGZDBSegmentPackedFile_F97005C, At<void*>(self, object::kIndex), &inserted[0], &entry);
            UpdateKeyCounts(self, nullptr, from->key.type, from->key.group, 1);
        }
        if (This<uint8_t>(addr::cGZDBSegmentPackedFile_F96FC95, self) != 0) {
            At<uint8_t>(self, object::kIndexDirty) = 0;
            ok = 1;
        }
    }
    Cdecl<void>(addr::SC4_operator_delete, entries);
    return ok;
}

// The embedded-package scan as the game's header reader calls it: through
// the call instruction at 0x009729E1, whose target a mod may have replaced.
uint32_t CallScanSite(char* self)
{
    const uint8_t* const site = reinterpret_cast<const uint8_t*>(kScanCallSite);
    std::uintptr_t target = addr::cGZDBSegmentPackedFile_F9725FB;
    if (site[0] == 0xE8) {
        int32_t relative;
        std::memcpy(&relative, site + 1, sizeof(relative));
        target = kScanCallSite + 5 + static_cast<std::uintptr_t>(relative);
    }
    return This<uint32_t>(target, self);
}

// The game's 0x00972999: reads the 0x60-byte header at the package's start
// and has it checked (0x0096E407). A header that fails the check sends the
// scan for an embedded package through the file; when that finds one, the
// header is read once more from there (the byte at 0x00B63638 stops a second
// round). A failed scan leaves the start at 0.
uint8_t __fastcall ReadHeader(char* self, void*)
{
    char* const stream = self + object::kFile;
    const uint32_t position = Virtual<uint32_t>(stream, file::kSeek, At<uint32_t>(self, object::kBase));
    if (position != At<uint32_t>(self, object::kBase))
        return 0;
    if (Virtual<uint8_t>(stream, file::kRead, self + object::kHeader, uint32_t{0x60}) == 0)
        return 0;
    if (This<uint8_t>(addr::cGZDBSegmentPackedFile_F96E407, self) != 0)
        return 1;
    const uint32_t start = CallScanSite(self);
    At<uint32_t>(self, object::kBase) = start;
    if (start == 0xFFFFFFFF) {
        At<uint32_t>(self, object::kBase) = 0;
        return 0;
    }
    uint8_t& again = Global<uint8_t>(addr::cGZDBSegmentPackedFile_sRereadingHeader);
    if (again != 0)
        return 0;
    again = 1;
    const uint8_t ok = ReadHeader(self, nullptr);
    again = 0;
    return ok;
}

// The game's 0x009728D5 (cdecl, two arguments): what the resource setup asks
// of every plugin file. 0 when the file starts with the package signature;
// with `scan`, the offset of a package embedded in it; else -1. The file is
// opened through cRZFile's own functions by address, as the original does,
// and the scan runs on a segment made for it.
int32_t __cdecl ProbeFile(void* path, uint32_t scan)
{
    int32_t result = -1;
    alignas(4) char stream[0x80];
    This<void*>(addr::cRZFile_Construct, &stream[0], path);
    if (This<uint8_t>(addr::cRZFile_Open, &stream[0], uint32_t{1}, uint32_t{2}, uint32_t{1}) != 0) {
        uint32_t signature = 0;
        This<void>(addr::cRZFile_SeekToBegin, &stream[0]);
        if (This<uint8_t>(addr::cRZFile_Read, &stream[0], &signature, uint32_t{4}) != 0) {
            if (signature == Global<uint32_t>(addr::cGZDBSegmentPackedFile_kSignature)) {
                result = 0;
            } else if (static_cast<uint8_t>(scan) != 0) {
                char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C8});
                void* const made = memory ? Construct(memory, nullptr) : nullptr;
                void* holder = nullptr;
                This<void*>(addr::cGZDBSegmentPackedFile_F96E773, &holder, made);
                char* const segment = static_cast<char*>(holder);
                Virtual<uint8_t>(segment, 0x10);
                Virtual<void>(segment, 0x24, path);
                result = static_cast<int32_t>(ScanEmbedded(segment, nullptr));
                Virtual<uint32_t>(segment, 0x08);
            }
        }
        This<void>(addr::cRZFile_Close, &stream[0]);
    }
    This<void>(addr::cRZFile_Destruct, &stream[0]);
    return result;
}

// The game's 0x00972A17, slot 6 of the interface (Open). `self` is the
// interface. Needs a closed, initialised segment with a path and at least
// one of `read` and `write`. Reading: the header, the index, the holes
// (0x0096FCE8), the directory. Writing: an empty file gets a fresh header
// (0x0096E99A), and so does a file whose header cannot be read. On a failure
// after the file opened, the file is closed; the flags stay as set, as in
// the original (only a file that does not open clears them). RET 8.
uint8_t __fastcall Open(char* self, void*, uint32_t read, uint32_t write)
{
    uint8_t ok = 0;
    if ((At<uint8_t>(self, iface::kFlags) & 3) != 0)
        return ok;
    if (At<uint32_t>(self, iface::kPathBegin) == At<uint32_t>(self, iface::kPathEnd))
        return ok;
    if (At<uint8_t>(self, iface::kInitialised) == 0)
        return ok;
    if (static_cast<uint8_t>(read) == 0 && static_cast<uint8_t>(write) == 0)
        return ok;
    uint32_t access = 0;
    uint32_t mode = 2;
    At<uint32_t>(self, iface::kFlags) = 0;
    if (static_cast<uint8_t>(read) != 0) {
        access = 1;
        At<uint32_t>(self, iface::kFlags) = 1;
        mode = 2;
    }
    if (static_cast<uint8_t>(write) != 0) {
        At<uint32_t>(self, iface::kFlags) |= 3;
        access |= 3;
        mode = 3;
    }
    char* const stream = self + iface::kFile;
    char* const start = self - 4;
    Virtual<void>(stream, file::kSetPath, self + iface::kPath);
    if (Virtual<uint8_t>(stream, file::kOpen, access, mode, uint32_t{1}) == 0) {
        At<uint32_t>(self, iface::kFlags) = 0;
        return ok;
    }
    bool proceed = true;
    if (Virtual<uint32_t>(stream, file::kLength) == 0) {
        if ((At<uint8_t>(self, iface::kFlags) & 2) != 0)
            ok = This<uint8_t>(addr::cGZDBSegmentPackedFile_F96E99A, start);
        else
            ok = 0;
        proceed = ok != 0;
    }
    if (proceed) {
        ok = ReadHeader(start, nullptr);
        if (ok != 0) {
            ok = ReadIndex(start, nullptr);
            if (ok != 0) {
                ok = This<uint8_t>(addr::cGZDBSegmentPackedFile_F96FCE8, start);
                if (ok != 0)
                    ok = ReadDirectory(start, nullptr);
            }
        } else if ((At<uint8_t>(self, iface::kFlags) & 2) != 0) {
            ok = This<uint8_t>(addr::cGZDBSegmentPackedFile_F96E99A, start);
            if (ok != 0 && At<void*>(self, iface::kIndex) == nullptr)
                At<void*>(self, iface::kIndex) = NewIndexTable();
        }
    }
    if (ok == 0)
        Virtual<uint8_t>(stream, file::kClose);
    return ok;
}

// From here on: the segment base class (vtable 0x00AD8CA0), whose slots the
// packed file and the other segment classes share. `self` is the interface:
// +4 the reference count, +8 the lock.

// The game's 0x009756AA, slot 0: QueryInterface. RET 8.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t id, void** out)
{
    if (id == kSegmentInterface || id == 1) {
        *out = self;
        Virtual<uint32_t>(self, slot::kAddRef);
        return 1;
    }
    return 0;
}

// The game's 0x009756F6, slot 2: Release, under the lock. The last
// reference unlocks and then calls slot 31, which deletes the object.
uint32_t __fastcall Release(char* self, void*)
{
    char* const lockObject = self + iface::kLock;
    Virtual<void>(lockObject, lock::kLock);
    const uint32_t count = At<uint32_t>(self, iface::kCount);
    if (count > 1) {
        At<uint32_t>(self, iface::kCount) = count - 1;
        Virtual<uint32_t>(lockObject, lock::kUnlock);
        return count - 1;
    }
    Virtual<uint32_t>(lockObject, lock::kUnlock);
    Virtual<void>(self, slot::kDeleteSelf);
    return 0;
}

// The game's 0x0097572E, the base's slot 31: deletes the object through its
// deleting destructor (slot 30). The packed file's own slot 31 (0x0096DF3C)
// ends by jumping here. What the destructor leaves in EAX is passed on.
uint32_t __fastcall DeleteSelf(char* self, void*)
{
    if (self)
        return Virtual<uint32_t>(self, slot::kDeletingDestructor, uint32_t{1});
    return 0; // the original leaves EAX as it was
}

// One of the interface's public calls: the unlocked slot under the lock.
template <typename R, typename... Args>
R Locked(char* self, std::size_t unlocked, Args... args)
{
    LockGuard guard;
    Lock(guard, self + iface::kLock);
    const R result = Virtual<R>(self, unlocked, args...);
    Unlock(guard);
    return result;
}

// The game's 0x0097573A, slot 19: TestForRecord(key). RET 4.
uint8_t __fastcall TestForRecord(char* self, void*, uint32_t key)
{
    return Locked<uint8_t>(self, slot::kTestForRecord, key);
}

// The game's 0x0097576E, slot 20: GetRecordSize(key). RET 4.
uint32_t __fastcall GetRecordSize(char* self, void*, uint32_t key)
{
    return Locked<uint32_t>(self, slot::kGetRecordSize, key);
}

// The game's 0x009757A0, slot 21: OpenRecord(key, record out, flags).
// RET 0xC.
uint8_t __fastcall OpenRecord(char* self, void*, uint32_t key, uint32_t out, uint32_t flags)
{
    return Locked<uint8_t>(self, slot::kOpenRecord, key, out, flags);
}

// The game's 0x009757DA, slot 22: CreateNewRecord(key, record out). RET 8.
uint8_t __fastcall CreateNewRecord(char* self, void*, uint32_t key, uint32_t out)
{
    return Locked<uint8_t>(self, slot::kCreateNewRecord, key, out);
}

// The game's 0x00975811, slot 24: CloseRecord(record). RET 4.
uint8_t __fastcall CloseRecordLocked(char* self, void*, uint32_t record)
{
    return Locked<uint8_t>(self, slot::kCloseRecordUnlocked, record);
}

// The game's 0x00975845, slot 23: CloseRecord on a held reference: closes
// through slot 24 and, when that worked, releases the record. The caller's
// pointer is not cleared. RET 4.
uint8_t __fastcall CloseRecordReference(char* self, void*, void** reference)
{
    void* const record = *reference;
    if (Virtual<uint8_t>(self, slot::kCloseRecord, record) == 0)
        return 0;
    Virtual<uint32_t>(record, 0x08);
    return 1;
}

// The game's 0x00975867, slot 26: AbortRecord(record). RET 4.
uint8_t __fastcall AbortRecordLocked(char* self, void*, uint32_t record)
{
    return Locked<uint8_t>(self, slot::kAbortRecordUnlocked, record);
}

// The game's 0x0097589B, slot 25: AbortRecord on a held reference: aborts
// through slot 26 and, when that worked, releases the record. RET 4.
uint8_t __fastcall AbortRecordReference(char* self, void*, void** reference)
{
    void* const record = *reference;
    if (Virtual<uint8_t>(self, slot::kAbortRecord, record) == 0)
        return 0;
    Virtual<uint32_t>(record, 0x08);
    return 1;
}

// The game's 0x009758BD, slot 27: DeleteRecord(key). RET 4.
uint8_t __fastcall DeleteRecord(char* self, void*, uint32_t key)
{
    return Locked<uint8_t>(self, slot::kDeleteRecord, key);
}

// The game's 0x009758F1, slot 28: ReadRecord(key, buffer, size in and out);
// the result is a dword. RET 0xC.
uint32_t __fastcall ReadRecord(char* self, void*, uint32_t key, uint32_t buffer, uint32_t size)
{
    return Locked<uint32_t>(self, slot::kReadRecord, key, buffer, size);
}

// The game's 0x00975929, slot 29: WriteRecord(key, data, size). RET 0xC.
uint8_t __fastcall WriteRecord(char* self, void*, uint32_t key, uint32_t data, uint32_t size)
{
    return Locked<uint8_t>(self, slot::kWriteRecord, key, data, size);
}

// The game's 0x00975963, slot 17: whether slot 18 (a count of records for
// the argument, second argument 0) gives anything; the whole dword is
// tested and the result is a whole dword. RET 4.
uint32_t __fastcall HasRecords(char* self, void*, uint32_t which)
{
    return Virtual<uint32_t>(self, slot::kCountRecords, which, uint32_t{0}) != 0 ? 1 : 0;
}

// The game's 0x00975977, slot 12: takes the segment's lock.
uint8_t __fastcall LockSegment(char* self, void*)
{
    Virtual<void>(self + iface::kLock, lock::kLock);
    return 1;
}

// The game's 0x00975982, slot 13: gives the segment's lock back.
uint8_t __fastcall UnlockSegment(char* self, void*)
{
    Virtual<uint32_t>(self + iface::kLock, lock::kUnlock);
    return 1;
}

bool InstallPackedFile4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZDBSegmentPackedFile_Close, reinterpret_cast<void*>(&Close)},
        {addr::cGZDBSegmentPackedFile_F97139C, reinterpret_cast<void*>(&UpdateKeyCounts)},
        {addr::cGZDBSegmentPackedFile_F971481, reinterpret_cast<void*>(&SetKeyTracking)},
        {addr::cGZDBSegmentPackedFile_F9718B9, reinterpret_cast<void*>(&ReadDirectory)},
        {addr::cGZDBSegmentPackedFile_F971991, reinterpret_cast<void*>(&CloseRecord)},
        {addr::cGZDBSegmentPackedFile_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cGZDBSegmentPackedFile_F9725FB, reinterpret_cast<void*>(&ScanEmbedded)},
        {addr::cGZDBSegmentPackedFile_F972761, reinterpret_cast<void*>(&ReadIndex)},
        {addr::cGZDBSegmentPackedFile_F9728D5, reinterpret_cast<void*>(&ProbeFile)},
        {addr::cGZDBSegmentPackedFile_F972999, reinterpret_cast<void*>(&ReadHeader)},
        {addr::cGZDBSegmentPackedFile_Open, reinterpret_cast<void*>(&Open)},
        {addr::cGZDBSegmentPackedFile_F9756AA, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cGZDBSegmentPackedFile_F9756F6, reinterpret_cast<void*>(&Release)},
        {addr::cGZDBSegmentPackedFile_F97572E, reinterpret_cast<void*>(&DeleteSelf)},
        {addr::cGZDBSegmentPackedFile_F97573A, reinterpret_cast<void*>(&TestForRecord)},
        {addr::cGZDBSegmentPackedFile_F97576E, reinterpret_cast<void*>(&GetRecordSize)},
        {addr::cGZDBSegmentPackedFile_F9757A0, reinterpret_cast<void*>(&OpenRecord)},
        {addr::cGZDBSegmentPackedFile_F9757DA, reinterpret_cast<void*>(&CreateNewRecord)},
        {addr::cGZDBSegmentPackedFile_F975811, reinterpret_cast<void*>(&CloseRecordLocked)},
        {addr::cGZDBSegmentPackedFile_F975845, reinterpret_cast<void*>(&CloseRecordReference)},
        {addr::cGZDBSegmentPackedFile_F975867, reinterpret_cast<void*>(&AbortRecordLocked)},
        {addr::cGZDBSegmentPackedFile_F97589B, reinterpret_cast<void*>(&AbortRecordReference)},
        {addr::cGZDBSegmentPackedFile_F9758BD, reinterpret_cast<void*>(&DeleteRecord)},
        {addr::cGZDBSegmentPackedFile_F9758F1, reinterpret_cast<void*>(&ReadRecord)},
        {addr::cGZDBSegmentPackedFile_F975929, reinterpret_cast<void*>(&WriteRecord)},
        {addr::cGZDBSegmentPackedFile_F975963, reinterpret_cast<void*>(&HasRecords)},
        {addr::cGZDBSegmentPackedFile_F975977, reinterpret_cast<void*>(&LockSegment)},
        {addr::cGZDBSegmentPackedFile_F975982, reinterpret_cast<void*>(&UnlockSegment)},
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

const bool g_packedFile4Registered = registry::Register({
    "resources.packed_file.part4",
    "resources",
    registry::Kind::Detour,
    registry::Phase::PreAppInit,
    addr::cGZDBSegmentPackedFile_Open,
    "Our packed file, part 4: Open, Close, the header, index and directory readers, the embedded-package scan, "
    "CloseRecord, the constructor, the locked public calls",
    false,
    InstallPackedFile4,
});

} // namespace
} // namespace opensc4::packed_file
