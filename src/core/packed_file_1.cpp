// The framework's packed file, cGZDBSegmentPackedFile (docs/resources.md),
// read side, part 1: 0x0096D80C to 0x0096E99A. Replacement, off by default:
//
//   resources.packed_file.part1: the read records' own slots, constructors
//     and destructors, the raw file view, the segment's small methods
//     (QueryInterface, Init, Shutdown, the getters and setters, the file-span
//     readers), header and holes validation, and decompression.
//
// Three classes meet in this range.
//
// The records. A record is the framework's serial record (constructor
// 0x00975CB5, destructor 0x0096D7D4, typed getters from 0x009759C5 on: not
// this file's) with a kind on top:
//   +0x00 main vtable (63 slots: +0x6C GetVoid, +0xCC SetVoid, +0xD0 size,
//         +0xD8 position, +0xDC seek to a position, +0xE0 seek by an amount,
//         +0xF8 'the segment closed me')
//   +0x04 cIGZPersistDBRecord (16 slots; `this` of 0x0096D80C, 0x0096D824
//         and 0x0096D829 is here): +0x0A access word (1 while open for
//         reading, 0 once closed), +0x0C key, +0x18 the segment's
//         cIGZPersistDBSegment (counted)
//   +0x1C, +0x2C the input and output stream parts
//   +0x3C a byte, the constructor's last argument
//   +0x40 kind: 1 file-backed, 2 in memory, 3 the write record (not ours)
//   kind 1: +0x44 offset in the package, +0x48 position, +0x4C size
//   kind 2: +0x44 a shared {data*, count} of 8 bytes, +0x48 position,
//           +0x4C size
// Vtables: base 0x00AD7EE8 / 0x00AD7EA8 / 0x00AD7E48 / 0x00AD7DE8, kind 1
// 0x00AD80E8 / 0x00AD80A8 / 0x00AD8048 / 0x00AD7FE8, kind 2 0x00AD82E8 /
// 0x00AD82A8 / 0x00AD8248 / 0x00AD81E8. The deleting destructors of kinds 1
// and 2 push the block on its pool's free list (0x00B17A00, 0x00B17A10).
//
// The raw file view (0x10 bytes; constructor 0x0096EC7C): a file object
// whose calls go to a record.
//   +0x00 main vtable 0x00AD8468 (QueryInterface, AddRef, Release, set the
//         record, ..., the deleting destructor at +0x18)
//   +0x04 the file interface, vtable 0x00AD83E8 (`this` of the slots from
//         0x0096DCBC to 0x0096DEC1 is here)
//   +0x08 reference count
//   +0x0C the record's main part (counted)
//
// The segment (0x1C8 bytes): +0x00 main vtable 0x00AD87D0, +0x04 the
// cIGZPersistDBSegment vtable 0x00AD8728, +0x08 reference count, +0x0C the
// lock, +0x14 'initialised' byte, +0x18 segment ID, +0x1C open flags (bit 0
// read, bit 1 write), +0x2C the path (a game string: vtable, begin +0x30,
// end +0x34), +0x40 the file (an embedded cRZFile), +0xC0 the package's
// offset in the file, +0xC4 and +0xC8 the file's two buffer sizes as last
// set, +0xCC and +0xD0 the user version to write, +0xE4 the 0x60-byte
// header as read, +0x1C4 a scratch buffer of 0x800 bytes for compressed
// records.
//
// Decompress (0x0096E617) has a real catch block: catch everything, around
// the allocation of the output buffer only; it clears the caller's pointer
// and execution goes on after the allocation. Here that is `GuardedNew`.
//
// Still the game's: the write side (0x0096DBA0, 0x0096DBBC, 0x0096DBF0,
// 0x0096E15F, 0x0096E18B, 0x0096E2A7, 0x0096E337, 0x0096E37B, 0x0096E4DC,
// 0x0096E91B); the this-adjusting thunks (0x0096DF2C, 0x0096DF34,
// 0x0096E7AB to 0x0096E88B, 0x0096E963), 0x0096D8B9 and 0x0096E960, too
// short for a detour; 0x0096E773 (a counted pointer's constructor, called
// all over the framework) and 0x0096E957 (folded with a slot of an unrelated
// class, vtable 0x00AD7690).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::packed_file {
namespace {

namespace unknown { // cIGZUnknown, by vtable offset
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
} // namespace unknown

namespace lock { // the segment's lock, by vtable offset
constexpr std::size_t kUnlock = 0x8;
} // namespace lock

namespace file { // the embedded cRZFile, by vtable offset
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSeekToPosition = 0x30; // (position), answers the position reached
constexpr std::size_t kRead = 0x38;           // (data, size)
constexpr std::size_t kReadWithCount = 0x3C;  // (data, uint32_t* count)
constexpr std::size_t kSetBuffering = 0x7C;   // (read size, write size)
} // namespace file

namespace string { // the game's string, by vtable offset
constexpr std::size_t kFromChar = 0xC; // (text, length)
constexpr std::size_t kCopy = 0x34;    // (string)
// Fields.
constexpr std::size_t kBegin = 0x4;
constexpr std::size_t kEnd = 0x8;
} // namespace string

namespace record { // a record, from its main part
// Main vtable offsets.
constexpr std::size_t kAsDBRecord = 0xC;
constexpr std::size_t kGetVoid = 0x6C; // (data, size)
constexpr std::size_t kSetVoid = 0xCC; // (data, size)
constexpr std::size_t kGetSize = 0xD0;
constexpr std::size_t kSetSize = 0xD4; // (size)
constexpr std::size_t kGetPosition = 0xD8;
constexpr std::size_t kSeekAbsolute = 0xDC; // (position)
constexpr std::size_t kSeekRelative = 0xE0; // (amount)
constexpr std::size_t kClosed = 0xF8;
// Fields.
constexpr std::size_t kVtable = 0x0;
constexpr std::size_t kVtable4 = 0x4;
constexpr std::size_t kAccess = 0xA;
constexpr std::size_t kSegment = 0x18;
constexpr std::size_t kVtable1C = 0x1C;
constexpr std::size_t kVtable2C = 0x2C;
constexpr std::size_t kKind = 0x40;
constexpr std::size_t kOffset = 0x44; // kind 1
constexpr std::size_t kShared = 0x44; // kind 2
constexpr std::size_t kPosition = 0x48;
constexpr std::size_t kSize = 0x4C;
} // namespace record

namespace dbrecord { // cIGZPersistDBRecord (record + 4)
// Vtable offsets.
constexpr std::size_t kGetAccess = 0x10;
constexpr std::size_t kClose = 0x14;
// Fields, from the interface.
constexpr std::size_t kAccess = 0x6;
constexpr std::size_t kKey = 0x8;
} // namespace dbrecord

namespace view { // the raw file view
// Main vtable offsets.
constexpr std::size_t kDelete = 0x18; // (flags)
// File interface vtable offsets (view + 4).
constexpr std::size_t kIsOpen = 0x10;
constexpr std::size_t kClose = 0x14;
constexpr std::size_t kSeekRelative = 0x2C;   // (amount)
constexpr std::size_t kSeekToPosition = 0x30; // (position)
// Fields, from the view.
constexpr std::size_t kVtable = 0x0;
constexpr std::size_t kFile = 0x4;
constexpr std::size_t kRefCount = 0x8;
constexpr std::size_t kRecord = 0xC;
// The record, from the file interface.
constexpr std::size_t kFileRecord = 0x8;
} // namespace view

namespace segment { // cGZDBSegmentPackedFile, from the object
// Main vtable offsets.
constexpr std::size_t kShutdown = 0x14;
constexpr std::size_t kReadFile = 0xB0; // (data, offset in the package, size)
// cIGZPersistDBSegment vtable offsets (object + 4).
constexpr std::size_t kClose = 0x20;
// Fields.
constexpr std::size_t kInterface = 0x4;
constexpr std::size_t kLock = 0xC;
constexpr std::size_t kInitialised = 0x14;
constexpr std::size_t kSegmentId = 0x18;
constexpr std::size_t kOpenFlags = 0x1C;
constexpr std::size_t kPath = 0x2C;
constexpr std::size_t kFile = 0x40;
constexpr std::size_t kBaseOffset = 0xC0;
constexpr std::size_t kReadBuffer = 0xC4;
constexpr std::size_t kWriteBuffer = 0xC8;
constexpr std::size_t kUserMajor = 0xCC;
constexpr std::size_t kUserMinor = 0xD0;
constexpr std::size_t kHeader = 0xE4;
constexpr std::size_t kHeaderMajor = 0xE8;
constexpr std::size_t kHeaderUserMajor = 0xF0;
constexpr std::size_t kHeaderUserMinor = 0xF4;
constexpr std::size_t kHeaderCreated = 0xFC;
constexpr std::size_t kHeaderModified = 0x100;
constexpr std::size_t kIndexCount = 0x108;
constexpr std::size_t kIndexOffset = 0x10C;
constexpr std::size_t kIndexSize = 0x110;
constexpr std::size_t kHoleCount = 0x114;
constexpr std::size_t kHoleOffset = 0x118;
constexpr std::size_t kHoleSize = 0x11C;
constexpr std::size_t kScratch = 0x1C4;
} // namespace segment

constexpr uint32_t kSegmentIid = 0x286D4A6A;
constexpr uint32_t kViewFileIid = 0xE0203645;
constexpr uint32_t kViewIid = 0x8A32C0AC;
constexpr uint32_t kCountLimit = 0x7FFFFFF;
constexpr uint32_t kHeaderSize = 0x60;
constexpr uint32_t kScratchSize = 0x800;

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
static_assert(sizeof(ResourceKey) == 0xC);

// One entry of the index: a key and where its record is.
struct IndexEntry {
    ResourceKey key;
    uint32_t offset;
    uint32_t size;
};
static_assert(sizeof(IndexEntry) == 0x14);

// What in-memory records of one key share: the record's bytes and how many
// records use them.
struct SharedBuffer {
    void* data;
    int32_t count;
};
static_assert(sizeof(SharedBuffer) == 8);

// The scope guard 0x0048C9F0 builds: its vtable and the lock it took. The
// functions here end it inline with the lock's unlock slot.
struct LockGuard {
    std::uintptr_t vtable;
    void* held;
};
static_assert(sizeof(LockGuard) == 8);

// The game's decompressor (constructor 0x00A6E193), built on the stack.
struct Compressor {
    std::uintptr_t vtable;
    uint32_t refCount;
    uint8_t flag;
};
static_assert(sizeof(Compressor) == 0xC);

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

void* New(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_operator_new, size);
}

void Delete(void* block)
{
    Cdecl<void>(addr::SC4_operator_delete, block);
}

void Lock(LockGuard* guard, char* self)
{
    This<void*>(addr::SC4LockGuard_Construct, guard, static_cast<void*>(self + segment::kLock));
}

void Unlock(LockGuard* guard)
{
    Virtual<void>(guard->held, lock::kUnlock);
}

// The segment's cIGZPersistDBSegment for a segment, as the constructors
// convert it: nothing stays nothing.
char* InterfaceOf(char* segmentObject)
{
    return segmentObject ? segmentObject + segment::kInterface : nullptr;
}

// ---------------------------------------------------------------------------
// The records.

// The game's 0x0096D80C: slot 14 of a record's cIGZPersistDBRecord, `this`
// the interface. Sets the key.
void __fastcall RecordSetKey(char* self, void*, const ResourceKey* key)
{
    At<uint32_t>(self, dbrecord::kKey) = key->type;
    At<uint32_t>(self, dbrecord::kKey + 4) = key->group;
    At<uint32_t>(self, dbrecord::kKey + 8) = key->instance;
}

// The game's 0x0096D824: slot 4 of the same interface. The access word.
uint32_t __fastcall RecordGetAccess(char* self, void*)
{
    return At<uint16_t>(self, dbrecord::kAccess);
}

// The game's 0x0096D829: slot 15 of the same interface. Sets the access
// word from the low word of its argument.
void __fastcall RecordSetAccess(char* self, void*, uint32_t access)
{
    At<uint16_t>(self, dbrecord::kAccess) = static_cast<uint16_t>(access);
}

// The game's 0x0096D835: drops one use of a shared buffer, `this` the
// {data*, count}. The last use frees the data and then the pair. Answers
// the uses left.
uint32_t __fastcall SharedBufferRelease(SharedBuffer* self, void*)
{
    const int32_t count = self->count;
    if (count > 1) {
        self->count = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Delete(self->data);
    Delete(self);
    return 0;
}

// The game's 0x0096D859: the constructor all three kinds of record start
// with. The key, the segment's interface and the last argument go to the
// framework's serial record.
char* __fastcall RecordConstruct(char* self, void*, uint32_t kind, const ResourceKey* key, char* segmentInterface,
                                 uint32_t streamFlag)
{
    This<void*>(addr::cGZPersistDBSerialRecord_Construct, self, key, segmentInterface, streamFlag);
    At<uint32_t>(self, record::kKind) = kind;
    At<std::uintptr_t>(self, record::kVtable) = addr::cGZDBPackedRecord_Vtable;
    At<std::uintptr_t>(self, record::kVtable4) = addr::cGZDBPackedRecord_Vtable4;
    At<std::uintptr_t>(self, record::kVtable1C) = addr::cGZDBPackedRecord_Vtable1C;
    At<std::uintptr_t>(self, record::kVtable2C) = addr::cGZDBPackedRecord_Vtable2C;
    return self;
}

// The game's 0x0096D895: whether two keys are the same, `this` one of them.
// The index's and the directory's hash tables compare with it.
uint32_t __fastcall KeyEquals(const ResourceKey* self, void*, const ResourceKey* other)
{
    if (self->type != other->type)
        return 0;
    if (self->group != other->group)
        return 0;
    if (self->instance != other->instance)
        return 0;
    return 1;
}

// The game's 0x0096D8BC: the constructor of the header kept at segment
// +0xE4: clears its signature.
void* __fastcall HeaderConstruct(void* self, void*)
{
    Cdecl<void*>(addr::CRT_memset, self, int32_t{0}, uint32_t{4});
    return self;
}

// The game's 0x0096D8D0: the constructor of a file-backed read record
// (kind 1): `offset` and `size` are the record's place in the package.
char* __fastcall FileRecordConstruct(char* self, void*, uint32_t offset, uint32_t size, const ResourceKey* key,
                                     char* segmentObject, uint32_t streamFlag)
{
    RecordConstruct(self, nullptr, 1, key, InterfaceOf(segmentObject), streamFlag);
    At<uint32_t>(self, record::kPosition) = 0;
    At<uint32_t>(self, record::kOffset) = offset;
    At<uint32_t>(self, record::kSize) = size;
    At<std::uintptr_t>(self, record::kVtable) = addr::cGZDBPackedFileRecord_Vtable;
    At<std::uintptr_t>(self, record::kVtable4) = addr::cGZDBPackedFileRecord_Vtable4;
    At<std::uintptr_t>(self, record::kVtable1C) = addr::cGZDBPackedFileRecord_Vtable1C;
    At<std::uintptr_t>(self, record::kVtable2C) = addr::cGZDBPackedFileRecord_Vtable2C;
    At<uint16_t>(self, record::kAccess) = 1;
    return self;
}

// The game's 0x0096D92C: slot 55 (+0xDC) of both read records. Seeks to a
// position before the end.
uint8_t __fastcall RecordSeekAbsolute(char* self, void*, uint32_t position)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    if (position >= At<uint32_t>(self, record::kSize))
        return 0;
    At<uint32_t>(self, record::kPosition) = position;
    return 1;
}

// The game's 0x0096D948: slot 56 (+0xE0) of the file-backed record. Seeks
// by an amount; the sum is compared unsigned.
uint8_t __fastcall FileRecordSeekRelative(char* self, void*, uint32_t amount)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    const uint32_t position = At<uint32_t>(self, record::kPosition) + amount;
    if (position >= At<uint32_t>(self, record::kSize))
        return 0;
    At<uint32_t>(self, record::kPosition) = position;
    return 1;
}

// The game's 0x0096D967: slot 27 (+0x6C) of the file-backed record. Reads
// `size` bytes at the position through the segment's file-span reader. A
// record without a segment calls through a null object, as the original.
uint8_t __fastcall FileRecordGetVoid(char* self, void*, void* data, uint32_t size)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    const uint32_t position = At<uint32_t>(self, record::kPosition);
    const uint32_t fileOffset = At<uint32_t>(self, record::kOffset) + position;
    if (position + size > At<uint32_t>(self, record::kSize))
        return 0;
    char* const segmentInterface = At<char*>(self, record::kSegment);
    char* const segmentObject = segmentInterface ? segmentInterface - segment::kInterface : nullptr;
    if (!Virtual<uint8_t>(segmentObject, segment::kReadFile, data, fileOffset, size))
        return 0;
    At<uint32_t>(self, record::kPosition) += size;
    return 1;
}

// The game's 0x0096D9B3: slot 62 (+0xF8) of the file-backed record, called
// when the segment closes it: the record is no longer open and lets go of
// the segment.
void __fastcall FileRecordClosed(char* self, void*)
{
    void* const segmentInterface = At<void*>(self, record::kSegment);
    At<uint16_t>(self, record::kAccess) = 0;
    if (segmentInterface) {
        Virtual<uint32_t>(segmentInterface, unknown::kRelease);
        At<void*>(self, record::kSegment) = nullptr;
    }
    At<uint32_t>(self, record::kPosition) = 0;
}

// The game's 0x0096D9D1: the constructor of an in-memory read record
// (kind 2) that shares the bytes of a record already open: one more use of
// `shared`. A null `shared` is written through, as the original
// (docs/crashes.md, "0x0096DA20").
char* __fastcall MemoryRecordConstructShared(char* self, void*, SharedBuffer* shared, uint32_t size,
                                             const ResourceKey* key, char* segmentObject, uint32_t streamFlag)
{
    RecordConstruct(self, nullptr, 2, key, InterfaceOf(segmentObject), streamFlag);
    At<uint32_t>(self, record::kPosition) = 0;
    At<SharedBuffer*>(self, record::kShared) = shared;
    At<std::uintptr_t>(self, record::kVtable) = addr::cGZDBPackedMemoryRecord_Vtable;
    At<std::uintptr_t>(self, record::kVtable4) = addr::cGZDBPackedMemoryRecord_Vtable4;
    At<std::uintptr_t>(self, record::kVtable1C) = addr::cGZDBPackedMemoryRecord_Vtable1C;
    At<std::uintptr_t>(self, record::kVtable2C) = addr::cGZDBPackedMemoryRecord_Vtable2C;
    At<uint32_t>(self, record::kSize) = size;
    shared->count = shared->count + 1;
    At<uint16_t>(self, record::kAccess) = 1;
    return self;
}

// The game's 0x0096DA30: the constructor of an in-memory read record
// (kind 2) over bytes it takes ownership of. A failed allocation of the
// shared pair is written through, as the original.
char* __fastcall MemoryRecordConstruct(char* self, void*, void* data, uint32_t size, const ResourceKey* key,
                                       char* segmentObject, uint32_t streamFlag)
{
    RecordConstruct(self, nullptr, 2, key, InterfaceOf(segmentObject), streamFlag);
    At<std::uintptr_t>(self, record::kVtable) = addr::cGZDBPackedMemoryRecord_Vtable;
    At<std::uintptr_t>(self, record::kVtable4) = addr::cGZDBPackedMemoryRecord_Vtable4;
    At<std::uintptr_t>(self, record::kVtable1C) = addr::cGZDBPackedMemoryRecord_Vtable1C;
    At<std::uintptr_t>(self, record::kVtable2C) = addr::cGZDBPackedMemoryRecord_Vtable2C;
    SharedBuffer* shared = static_cast<SharedBuffer*>(New(sizeof(SharedBuffer)));
    if (shared) {
        shared->count = 0;
        shared->data = data;
    }
    At<uint32_t>(self, record::kPosition) = 0;
    At<SharedBuffer*>(self, record::kShared) = shared;
    At<uint32_t>(self, record::kSize) = size;
    shared->count = shared->count + 1;
    At<uint16_t>(self, record::kAccess) = 1;
    return self;
}

// The game's 0x0096DAA5: the destructor of an in-memory read record: drops
// its use of the shared bytes, then the framework record's destructor.
void __fastcall MemoryRecordDestruct(char* self, void*)
{
    SharedBuffer* const shared = At<SharedBuffer*>(self, record::kShared);
    At<std::uintptr_t>(self, record::kVtable) = addr::cGZDBPackedMemoryRecord_Vtable;
    At<std::uintptr_t>(self, record::kVtable4) = addr::cGZDBPackedMemoryRecord_Vtable4;
    At<std::uintptr_t>(self, record::kVtable1C) = addr::cGZDBPackedMemoryRecord_Vtable1C;
    At<std::uintptr_t>(self, record::kVtable2C) = addr::cGZDBPackedMemoryRecord_Vtable2C;
    if (shared)
        SharedBufferRelease(shared, nullptr);
    This<void>(addr::cGZPersistDBSerialRecord_Destruct, self);
}

// The game's 0x0096DAD7: slot 56 (+0xE0) of the in-memory record. Seeks by
// a signed amount; the new position must be at least 0 and before the end.
uint8_t __fastcall MemoryRecordSeekRelative(char* self, void*, int32_t amount)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    const int32_t position = static_cast<int32_t>(At<uint32_t>(self, record::kPosition) + static_cast<uint32_t>(amount));
    if (position < 0)
        return 0;
    if (position >= At<int32_t>(self, record::kSize))
        return 0;
    At<int32_t>(self, record::kPosition) = position;
    return 1;
}

// The game's 0x0096DAF8: slot 27 (+0x6C) of the in-memory record. Copies
// `size` bytes at the position out; the end is compared signed and may
// reach the record's end.
uint8_t __fastcall MemoryRecordGetVoid(char* self, void*, void* data, uint32_t size)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    const uint32_t position = At<uint32_t>(self, record::kPosition);
    const int32_t end = static_cast<int32_t>(position + size);
    if (end > At<int32_t>(self, record::kSize))
        return 0;
    char* const bytes = static_cast<char*>(At<SharedBuffer*>(self, record::kShared)->data);
    Cdecl<void*>(addr::CRT_memcpy, data, static_cast<void*>(bytes + position), size);
    At<int32_t>(self, record::kPosition) = end;
    return 1;
}

// The game's 0x0096DB35: slot 51 (+0xCC) of the in-memory record. Copies
// `size` bytes in at the position; unlike the read, the end must stay
// before the record's end.
uint8_t __fastcall MemoryRecordSetVoid(char* self, void*, const void* data, uint32_t size)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    const uint32_t position = At<uint32_t>(self, record::kPosition);
    const int32_t end = static_cast<int32_t>(position + size);
    if (end >= At<int32_t>(self, record::kSize))
        return 0;
    char* const bytes = static_cast<char*>(At<SharedBuffer*>(self, record::kShared)->data);
    Cdecl<void*>(addr::CRT_memcpy, static_cast<void*>(bytes + position), data, size);
    At<int32_t>(self, record::kPosition) = end;
    return 1;
}

// The game's 0x0096DB72: slot 62 (+0xF8) of the in-memory record, called
// when the segment closes it: no longer open, lets go of the segment and of
// the shared bytes.
void __fastcall MemoryRecordClosed(char* self, void*)
{
    void* const segmentInterface = At<void*>(self, record::kSegment);
    At<uint16_t>(self, record::kAccess) = 0;
    if (segmentInterface) {
        Virtual<uint32_t>(segmentInterface, unknown::kRelease);
        At<void*>(self, record::kSegment) = nullptr;
    }
    SharedBuffer* const shared = At<SharedBuffer*>(self, record::kShared);
    if (shared) {
        SharedBufferRelease(shared, nullptr);
        At<SharedBuffer*>(self, record::kShared) = nullptr;
    }
    At<uint32_t>(self, record::kPosition) = 0;
}

// The game's 0x0096E893: the scalar deleting destructor of the record base.
char* __fastcall RecordDelete(char* self, void*, uint32_t flags)
{
    This<void>(addr::cGZPersistDBSerialRecord_Destruct, self);
    if (flags & 1)
        Delete(self);
    return self;
}

// The game's 0x0096E8D5: the destructor of a file-backed read record.
void __fastcall FileRecordDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, record::kVtable) = addr::cGZDBPackedFileRecord_Vtable;
    At<std::uintptr_t>(self, record::kVtable4) = addr::cGZDBPackedFileRecord_Vtable4;
    At<std::uintptr_t>(self, record::kVtable1C) = addr::cGZDBPackedFileRecord_Vtable1C;
    At<std::uintptr_t>(self, record::kVtable2C) = addr::cGZDBPackedFileRecord_Vtable2C;
    This<void>(addr::cGZPersistDBSerialRecord_Destruct, self);
}

// The game's 0x0096E8AF: the deleting destructor of a file-backed read
// record: the block goes back on its pool's free list.
char* __fastcall FileRecordDelete(char* self, void*, uint32_t flags)
{
    FileRecordDestruct(self, nullptr);
    if ((flags & 1) && self) {
        At<void*>(self, 0) = Global<void*>(addr::cGZDBSegmentPackedFile_FileRecordFreeList);
        Global<void*>(addr::cGZDBSegmentPackedFile_FileRecordFreeList) = self;
    }
    return self;
}

// The game's 0x0096E8F5: the deleting destructor of an in-memory read
// record: the block goes back on its pool's free list.
char* __fastcall MemoryRecordDelete(char* self, void*, uint32_t flags)
{
    MemoryRecordDestruct(self, nullptr);
    if ((flags & 1) && self) {
        At<void*>(self, 0) = Global<void*>(addr::cGZDBSegmentPackedFile_MemoryRecordFreeList);
        Global<void*>(addr::cGZDBSegmentPackedFile_MemoryRecordFreeList) = self;
    }
    return self;
}

// The game's 0x0096E363: tells a record the segment closed it, `this` the
// segment (unused), the argument the record's cIGZPersistDBRecord. Calls
// through a null object for no record, as the original.
void __fastcall SegmentRecordClosed(char*, void*, char* recordInterface)
{
    char* const recordObject = recordInterface ? recordInterface - 4 : nullptr;
    Virtual<void>(recordObject, record::kClosed);
}

// ---------------------------------------------------------------------------
// The raw file view.

// The game's 0x0096DCBC: slot 5 (+0x14) of the view's file interface, `this`
// the interface. Closes the record through its own Close and lets it go.
uint8_t __fastcall ViewClose(char* self, void*)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    void* const recordInterface = Virtual<void*>(viewed, record::kAsDBRecord);
    Virtual<uint8_t>(recordInterface, dbrecord::kClose);
    Virtual<uint32_t>(At<void*>(self, view::kFileRecord), unknown::kRelease);
    At<void*>(self, view::kFileRecord) = nullptr;
    return 1;
}

// The game's 0x0096DC2D: QueryInterface of the view, `this` the view.
uint8_t __fastcall ViewQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kViewFileIid)
        *out = self ? self + view::kFile : nullptr;
    else if (iid == kViewIid || iid == 1)
        *out = self;
    else
        return 0;
    Virtual<uint32_t>(self, unknown::kAddRef);
    return 1;
}

// The game's 0x0096DC6E: what the view's Release does with the last
// reference: closes the file side when it is open, then the deleting
// destructor.
void __fastcall ViewFinalRelease(char* self, void*)
{
    char* const fileInterface = self + view::kFile;
    if (Virtual<uint8_t>(fileInterface, view::kIsOpen))
        Virtual<uint8_t>(fileInterface, view::kClose);
    Virtual<void*>(self, view::kDelete, uint32_t{1});
}

// The game's 0x0096DC93: slot 3 of the view. Takes a record (one reference
// more) in place of the one it has (one less).
uint8_t __fastcall ViewSetRecord(char* self, void*, void* newRecord)
{
    if (newRecord)
        Virtual<uint32_t>(newRecord, unknown::kAddRef);
    void* const old = At<void*>(self, view::kRecord);
    if (old)
        Virtual<uint32_t>(old, unknown::kRelease);
    At<void*>(self, view::kRecord) = newRecord;
    return 1;
}

// The game's 0x0096DCE4: slot 6 of the file interface. The record's
// position, -1 without a record.
uint32_t __fastcall ViewPosition(char* self, void*)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0xFFFFFFFFu;
    return Virtual<uint32_t>(viewed, record::kGetPosition);
}

// The game's 0x0096DCF9: slot 7. The record's size, 0 without a record.
uint32_t __fastcall ViewLength(char* self, void*)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    return Virtual<uint32_t>(viewed, record::kGetSize);
}

// The game's 0x0096DD0C: slot 8. Passes its argument to the record's slot
// +0xD4 (which refuses, for the read records).
uint32_t __fastcall ViewSetLength(char* self, void*, uint32_t length)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    return Virtual<uint32_t>(viewed, record::kSetSize, length);
}

// The game's 0x0096DD22: slot 9. Seeks the record to its start: 0, or -1
// when that fails.
uint32_t __fastcall ViewSeekToBegin(char* self, void*)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (viewed && Virtual<uint8_t>(viewed, record::kSeekAbsolute, uint32_t{0}))
        return 0;
    return 0xFFFFFFFFu;
}

// The game's 0x0096DD40: slot 10. Seeks the record to its size and answers
// it; -1 for an empty record or a refused seek (the read records refuse a
// position at their end).
uint32_t __fastcall ViewSeekToEnd(char* self, void*)
{
    if (!At<void*>(self, view::kFileRecord))
        return 0xFFFFFFFFu;
    const uint32_t size = Virtual<uint32_t>(At<void*>(self, view::kFileRecord), record::kGetSize);
    if (size == 0)
        return 0xFFFFFFFFu;
    if (!Virtual<uint8_t>(At<void*>(self, view::kFileRecord), record::kSeekAbsolute, size))
        return 0xFFFFFFFFu;
    return size;
}

// The game's 0x0096DD75: slot 11 (+0x2C). Seeks the record by an amount and
// answers the new position, or -1.
uint32_t __fastcall ViewSeekRelative(char* self, void*, uint32_t amount)
{
    if (!At<void*>(self, view::kFileRecord))
        return 0xFFFFFFFFu;
    if (!Virtual<uint8_t>(At<void*>(self, view::kFileRecord), record::kSeekRelative, amount))
        return 0xFFFFFFFFu;
    return Virtual<uint32_t>(At<void*>(self, view::kFileRecord), record::kGetPosition);
}

// The game's 0x0096DDA5: slot 12 (+0x30). Seeks the record to a position
// and answers it, or -1.
uint32_t __fastcall ViewSeekToPosition(char* self, void*, uint32_t position)
{
    if (!At<void*>(self, view::kFileRecord))
        return 0xFFFFFFFFu;
    if (!Virtual<uint8_t>(At<void*>(self, view::kFileRecord), record::kSeekAbsolute, position))
        return 0xFFFFFFFFu;
    return position;
}

// The game's 0x0096DDCA: slot 13. Seeks from the start (mode 0), from the
// position (1) or from the end (2) through the two slots above; -1 for
// another mode, no record, or an empty record in mode 2.
uint32_t __fastcall ViewSeek(char* self, void*, uint32_t offset, uint32_t mode)
{
    if (!At<void*>(self, view::kFileRecord))
        return 0xFFFFFFFFu;
    if (mode == 0)
        return Virtual<uint32_t>(self, view::kSeekToPosition, offset);
    if (mode == 1)
        return Virtual<uint32_t>(self, view::kSeekRelative, offset);
    if (mode != 2)
        return 0xFFFFFFFFu;
    const uint32_t size = Virtual<uint32_t>(At<void*>(self, view::kFileRecord), record::kGetSize);
    if (size == 0)
        return 0xFFFFFFFFu;
    return Virtual<uint32_t>(self, view::kSeekToPosition, size + offset);
}

// The game's 0x0096DE22: slot 14. The record's GetVoid.
uint32_t __fastcall ViewRead(char* self, void*, void* data, uint32_t size)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    return Virtual<uint32_t>(viewed, record::kGetVoid, data, size);
}

// The game's 0x0096DE35: slot 15. Reads up to `*count` bytes: the count is
// cut to what is left of the record first, and stays cut when the read
// fails.
uint8_t __fastcall ViewReadWithCount(char* self, void*, void* data, uint32_t* count)
{
    if (!At<void*>(self, view::kFileRecord))
        return 0;
    const uint32_t position = Virtual<uint32_t>(At<void*>(self, view::kFileRecord), record::kGetPosition);
    const uint32_t size = Virtual<uint32_t>(At<void*>(self, view::kFileRecord), record::kGetSize);
    if (*count + position > size)
        *count = size - position;
    return Virtual<uint8_t>(At<void*>(self, view::kFileRecord), record::kGetVoid, data, *count);
}

// The game's 0x0096DE7E: slot 16. The record's SetVoid.
uint32_t __fastcall ViewWrite(char* self, void*, const void* data, uint32_t size)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    return Virtual<uint32_t>(viewed, record::kSetVoid, data, size);
}

// The game's 0x0096DE94: slot 17. Writes `*count` bytes; a refused write
// leaves the count 0 and still answers true.
uint8_t __fastcall ViewWriteWithCount(char* self, void*, const void* data, uint32_t* count)
{
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    if (!Virtual<uint8_t>(viewed, record::kSetVoid, data, *count))
        *count = 0;
    return 1;
}

// The game's 0x0096DEC1: slot 29. The modes the 'file' is open with: access
// from the record's access word (bit 0 read, bit 1 write), then 3 and 1.
// The three outputs are written even without a record.
uint8_t __fastcall ViewGetOpenModes(char* self, void*, uint32_t* access, uint32_t* create, uint32_t* share)
{
    *access = 0;
    *create = 3;
    *share = 1;
    void* const viewed = At<void*>(self, view::kFileRecord);
    if (!viewed)
        return 0;
    void* const recordInterface = Virtual<void*>(viewed, record::kAsDBRecord);
    const uint32_t flags = Virtual<uint32_t>(recordInterface, dbrecord::kGetAccess);
    if (flags & 1)
        *access |= 1;
    if (flags & 2)
        *access |= 2;
    return 1;
}

// The game's 0x0096E93A: the view's destructor: closes the file side when
// it still has a record.
void __fastcall ViewDestruct(char* self, void*)
{
    const bool hasRecord = At<void*>(self, view::kRecord) != nullptr;
    At<std::uintptr_t>(self, view::kVtable) = addr::cGZDBSegmentPackedFile_RawFileViewVtable;
    At<std::uintptr_t>(self, view::kFile) = addr::cGZDBSegmentPackedFile_RawFileViewFileVtable;
    if (hasRecord)
        ViewClose(self + view::kFile, nullptr);
}

// The game's 0x0096E96B: Release of the view, `this` the view.
uint32_t __fastcall ViewRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, view::kRefCount);
    if (count > 1) {
        At<uint32_t>(self, view::kRefCount) = count - 1;
        return count - 1;
    }
    ViewFinalRelease(self, nullptr);
    return 0;
}

// ---------------------------------------------------------------------------
// The segment's small methods.

// The game's 0x0096DF0A: QueryInterface, slot 0 of the main vtable, `this`
// the segment. Answers 0x286D4A6A itself; everything else is the base
// class's, with the interface part.
uint8_t __fastcall SegmentQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kSegmentIid) {
        *out = self;
        Virtual<uint32_t>(self, unknown::kAddRef);
        return 1;
    }
    return This<uint8_t>(addr::cGZDBSegmentPackedFile_F9756AA, self + segment::kInterface, iid, out);
}

// The game's 0x0096DF3C: slot 31 (+0x7C) of the cIGZPersistDBSegment
// vtable, `this` the interface: what Release (0x009756F6) calls for the
// last reference. Closes an open segment, shuts an initialised one down,
// then deletes it (0x0097572E: the deleting destructor, slot +0x78).
void __fastcall SegmentFinalRelease(char* self, void*)
{
    if (At<uint8_t>(self, segment::kOpenFlags - segment::kInterface) & 3)
        Virtual<uint8_t>(self, segment::kClose);
    if (At<uint8_t>(self, segment::kInitialised - segment::kInterface) != 0)
        Virtual<uint8_t>(self - segment::kInterface, segment::kShutdown);
    This<void>(addr::cGZDBSegmentPackedFile_F97572E, self);
}

// The game's 0x0096DF60: Init, slot 3 of the cIGZPersistDBSegment vtable,
// `this` the interface. The first call takes the segment's ID and, when
// one is given, its path; later calls change nothing. The third argument is
// not read.
uint8_t __fastcall SegmentInit(char* self, void*, uint32_t segmentId, void* path, uint32_t)
{
    if (At<uint8_t>(self, segment::kInitialised - segment::kInterface) == 0) {
        At<uint8_t>(self, segment::kInitialised - segment::kInterface) = 1;
        if (path)
            Virtual<void>(self + (segment::kPath - segment::kInterface), string::kCopy, path);
        At<uint32_t>(self, segment::kSegmentId - segment::kInterface) = segmentId;
    }
    return 1;
}

// The game's 0x0096DF8D: Shutdown, slot 5 of the main vtable. Closes an
// open segment and frees the scratch buffer.
uint8_t __fastcall SegmentShutdown(char* self, void*)
{
    if (At<uint8_t>(self, segment::kInitialised) != 0) {
        const bool open = (At<uint8_t>(self, segment::kOpenFlags) & 3) != 0;
        At<uint8_t>(self, segment::kInitialised) = 0;
        if (open)
            Virtual<uint8_t>(self + segment::kInterface, segment::kClose);
        Delete(At<void*>(self, segment::kScratch));
        At<void*>(self, segment::kScratch) = nullptr;
    }
    return 1;
}

// The game's 0x0096DFBD: IsOpen, slot 7 of the cIGZPersistDBSegment vtable,
// `this` the interface.
uint32_t __fastcall SegmentIsOpen(char* self, void*)
{
    return (At<uint32_t>(self, segment::kOpenFlags - segment::kInterface) & 3) != 0 ? 1 : 0;
}

// The game's 0x0096DFC9: slot 15 of the cIGZPersistDBSegment vtable. Sets
// the segment's ID from the dword its argument points at.
uint8_t __fastcall SegmentSetId(char* self, void*, const uint32_t* segmentId)
{
    At<uint32_t>(self, segment::kSegmentId - segment::kInterface) = *segmentId;
    return 1;
}

// The game's 0x0096DFD7: slot 7 of the main vtable. Sets where the package
// starts in its file; refused while the segment is open.
uint8_t __fastcall SegmentSetBaseOffset(char* self, void*, uint32_t offset)
{
    if (At<uint8_t>(self, segment::kOpenFlags) & 3)
        return 0;
    At<uint32_t>(self, segment::kBaseOffset) = offset;
    return 1;
}

// The game's 0x0096E980: slot 8 of the main vtable. Copies the path into
// the caller's string.
uint32_t __fastcall SegmentGetPath(char* self, void*, void* out)
{
    char* const begin = At<char*>(self, segment::kPath + string::kBegin);
    const uint32_t length = static_cast<uint32_t>(At<char*>(self, segment::kPath + string::kEnd) - begin);
    return Virtual<uint32_t>(out, string::kFromChar, begin, length);
}

// The game's 0x0096DFF0: SetPath, slot 9 of the main vtable. Refused while
// the segment is open.
uint8_t __fastcall SegmentSetPath(char* self, void*, void* path)
{
    LockGuard guard;
    Lock(&guard, self);
    uint8_t result;
    if ((At<uint8_t>(self, segment::kOpenFlags) & 3) == 0) {
        Virtual<void>(self + segment::kPath, string::kCopy, path);
        result = 1;
    } else {
        result = 0;
    }
    Unlock(&guard);
    return result;
}

// The game's 0x0096E02C: slot 10 of the main vtable. The user version in
// the header as read.
void __fastcall SegmentGetHeaderUserVersion(char* self, void*, uint32_t* major, uint32_t* minor)
{
    LockGuard guard;
    Lock(&guard, self);
    *major = At<uint32_t>(self, segment::kHeaderUserMajor);
    *minor = At<uint32_t>(self, segment::kHeaderUserMinor);
    Unlock(&guard);
}

// The game's 0x0096E063: slot 11 of the main vtable. The user version a
// write of the header would store.
void __fastcall SegmentGetUserVersion(char* self, void*, uint32_t* major, uint32_t* minor)
{
    LockGuard guard;
    Lock(&guard, self);
    *major = At<uint32_t>(self, segment::kUserMajor);
    *minor = At<uint32_t>(self, segment::kUserMinor);
    Unlock(&guard);
}

// The game's 0x0096E09A: slot 12 of the main vtable. Sets that version.
void __fastcall SegmentSetUserVersion(char* self, void*, uint32_t major, uint32_t minor)
{
    LockGuard guard;
    Lock(&guard, self);
    At<uint32_t>(self, segment::kUserMajor) = major;
    At<uint32_t>(self, segment::kUserMinor) = minor;
    Unlock(&guard);
}

// The game's 0x0096E0CD: slot 42 of the main vtable. The two dates of the
// header as read (created, modified); false and nothing written while the
// segment is closed.
uint8_t __fastcall SegmentGetDates(char* self, void*, uint32_t* created, uint32_t* modified)
{
    LockGuard guard;
    Lock(&guard, self);
    uint8_t result = 0;
    if (At<uint8_t>(self, segment::kOpenFlags) & 3) {
        *created = At<uint32_t>(self, segment::kHeaderCreated);
        *modified = At<uint32_t>(self, segment::kHeaderModified);
        result = 1;
    }
    Unlock(&guard);
    return result;
}

// The game's 0x0096E112: slot 13 of the main vtable. Sets the file's two
// buffer sizes and, when the file took them, remembers them.
uint8_t __fastcall SegmentSetBuffering(char* self, void*, uint32_t readSize, uint32_t writeSize)
{
    LockGuard guard;
    Lock(&guard, self);
    uint8_t result;
    if (Virtual<uint8_t>(self + segment::kFile, file::kSetBuffering, readSize, writeSize)) {
        At<uint32_t>(self, segment::kReadBuffer) = readSize;
        At<uint32_t>(self, segment::kWriteBuffer) = writeSize;
        result = 1;
    } else {
        result = 0;
    }
    Unlock(&guard);
    return result;
}

// The game's 0x0096E198: slot 43 of the main vtable. Seeks the file to an
// offset in the package.
uint8_t __fastcall SegmentSeekFile(char* self, void*, uint32_t offset)
{
    LockGuard guard;
    Lock(&guard, self);
    uint8_t result;
    if (At<uint8_t>(self, segment::kOpenFlags) & 3) {
        char* const packageFile = self + segment::kFile;
        const uint32_t reached =
            Virtual<uint32_t>(packageFile, file::kSeekToPosition, At<uint32_t>(self, segment::kBaseOffset) + offset);
        result = reached == At<uint32_t>(self, segment::kBaseOffset) + offset ? 1 : 0;
    } else {
        result = 0;
    }
    Unlock(&guard);
    return result;
}

// The game's 0x0096E1E5: slot 44 (+0xB0) of the main vtable, the file-span
// reader: seeks the file to an offset in the package and reads `size`
// bytes.
uint8_t __fastcall SegmentReadFile(char* self, void*, void* data, uint32_t offset, uint32_t size)
{
    LockGuard guard;
    Lock(&guard, self);
    uint8_t result = 0;
    if (At<uint8_t>(self, segment::kOpenFlags) & 3) {
        char* const packageFile = self + segment::kFile;
        const uint32_t reached =
            Virtual<uint32_t>(packageFile, file::kSeekToPosition, At<uint32_t>(self, segment::kBaseOffset) + offset);
        if (reached == At<uint32_t>(self, segment::kBaseOffset) + offset
            && Virtual<uint8_t>(packageFile, file::kRead, data, size))
            result = 1;
    }
    Unlock(&guard);
    return result;
}

// The game's 0x0096E246: slot 45 of the main vtable. The same with the
// file's counted read: `*count` bytes asked, `*count` bytes got.
uint8_t __fastcall SegmentReadFileWithCount(char* self, void*, void* data, uint32_t offset, uint32_t* count)
{
    LockGuard guard;
    Lock(&guard, self);
    uint8_t result = 0;
    if (At<uint8_t>(self, segment::kOpenFlags) & 3) {
        char* const packageFile = self + segment::kFile;
        const uint32_t reached =
            Virtual<uint32_t>(packageFile, file::kSeekToPosition, At<uint32_t>(self, segment::kBaseOffset) + offset);
        if (reached == At<uint32_t>(self, segment::kBaseOffset) + offset
            && Virtual<uint8_t>(packageFile, file::kReadWithCount, data, count))
            result = 1;
    }
    Unlock(&guard);
    return result;
}

// The game's 0x0096E30D: slot 28 of the main vtable. Whether the segment is
// open, asked under the lock.
uint8_t __fastcall SegmentIsOpenLocked(char* self, void*)
{
    LockGuard guard;
    Lock(&guard, self);
    const uint8_t result = (At<uint8_t>(self, segment::kOpenFlags) & 3) != 0 ? 1 : 0;
    Unlock(&guard);
    return result;
}

// The game's 0x0096E407: whether the header just read is one of a package
// this code can open: the signature, a major version of at most 1, and an
// index and a hole table that lie inside the file with believable counts.
// The file's length is asked four times.
uint8_t __fastcall SegmentValidateHeader(char* self, void*)
{
    if (At<uint32_t>(self, segment::kHeader) != Global<uint32_t>(addr::cGZDBSegmentPackedFile_kSignature))
        return 0;
    if (At<uint32_t>(self, segment::kHeaderMajor) > 1)
        return 0;
    if (At<uint32_t>(self, segment::kIndexCount) >= kCountLimit)
        return 0;
    char* const packageFile = self + segment::kFile;
    if (At<uint32_t>(self, segment::kIndexOffset) >= Virtual<uint32_t>(packageFile, file::kLength))
        return 0;
    uint32_t length = Virtual<uint32_t>(packageFile, file::kLength);
    if (At<uint32_t>(self, segment::kIndexSize) + At<uint32_t>(self, segment::kIndexOffset) > length)
        return 0;
    if (At<uint32_t>(self, segment::kHoleCount) >= kCountLimit)
        return 0;
    if (At<uint32_t>(self, segment::kHoleOffset) >= Virtual<uint32_t>(packageFile, file::kLength))
        return 0;
    length = Virtual<uint32_t>(packageFile, file::kLength);
    if (At<uint32_t>(self, segment::kHoleSize) + At<uint32_t>(self, segment::kHoleOffset) > length)
        return 0;
    return 1;
}

// The game's 0x0096E49D: whether the header's hole table can be read: its
// offset is 0, or past the header and inside the file, and its count is its
// size in 8-byte entries.
uint8_t __fastcall SegmentValidateHoles(char* self, void*)
{
    uint8_t result = 1;
    const uint32_t holeOffset = At<uint32_t>(self, segment::kHoleOffset);
    if (holeOffset != 0) {
        if (holeOffset < kHeaderSize)
            result = 0;
        else if (!(At<uint32_t>(self, segment::kHoleOffset) < Virtual<uint32_t>(self + segment::kFile, file::kLength)))
            result = 0;
    }
    if (At<uint32_t>(self, segment::kHoleCount) != At<uint32_t>(self, segment::kHoleSize) >> 3)
        result = 0;
    return result;
}

// The game's 0x0096E78B: copies an index entry, `this` the destination.
IndexEntry* __fastcall IndexEntryCopy(IndexEntry* self, void*, const IndexEntry* from)
{
    self->key.type = from->key.type;
    self->key.group = from->key.group;
    self->key.instance = from->key.instance;
    self->offset = from->offset;
    self->size = from->size;
    return self;
}

// ---------------------------------------------------------------------------
// Decompression.

__declspec(noinline) void NewOutput(void** out, uint32_t* outSize, uint32_t size)
{
    *outSize = size;
    *out = New(size);
}

// The part of Decompress the original has inside its catch of everything:
// the size is stored, the buffer allocated and stored. The original's
// compiler (Visual C++ 7.1) catches structured exceptions in such a block
// too, so this is a structured handler, around a called function (crash.h,
// "SEH rule"). False when something was caught.
__declspec(noinline) bool GuardedNew(void** out, uint32_t* outSize, uint32_t size)
{
    __try {
        NewOutput(out, outSize, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game's 0x0096E617: decompresses `sourceSize` bytes into `*out`. With
// `*out` null a buffer of the uncompressed size is allocated (the caller
// frees it) and `*outSize` set; otherwise `*outSize` is the room in the
// caller's buffer and must be enough. On success `*outSize` is what the
// decompressor wrote. `this` is not read.
uint8_t __fastcall Decompress(char*, void*, void* source, uint32_t sourceSize, void** out, uint32_t* outSize)
{
    uint8_t result = 0;
    Compressor compressor;
    This<void*>(addr::SC4Compressor_Construct, &compressor);
    if (This<uint8_t>(addr::SC4Compressor_Validate, &compressor, source, sourceSize)) {
        const bool allocate = *out == nullptr;
        const uint32_t needed = This<uint32_t>(addr::SC4Compressor_GetUncompressedSize, &compressor, source);
        if (allocate && !GuardedNew(out, outSize, needed))
            *out = nullptr;
        void* const buffer = *out;
        if (buffer) {
            const uint32_t room = *outSize;
            if (room >= needed) {
                if (sourceSize == 0 || room == 0
                    || This<uint8_t>(addr::SC4Compressor_Decompress, &compressor, source, sourceSize, buffer, outSize)) {
                    result = 1;
                } else {
                    *outSize = 0;
                    if (allocate) {
                        Delete(*out);
                        *out = nullptr;
                    }
                }
            }
        }
    }
    This<void>(addr::SC4Compressor_Destruct, &compressor);
    return result;
}

// The game's 0x0096E6F4: reads a compressed record of `size` bytes at
// `offset` in the package and decompresses it as above. Up to 0x800 bytes
// go through the segment's scratch buffer (made on first use, kept), more
// through a buffer of their own. An offset of 0 fails.
uint8_t __fastcall SegmentReadCompressed(char* self, void*, uint32_t offset, uint32_t size, void** out,
                                         uint32_t* outSize)
{
    uint8_t result = 0;
    if (offset != 0) {
        void* buffer;
        void* owned;
        if (size <= kScratchSize) {
            void*& scratch = At<void*>(self, segment::kScratch);
            if (!scratch)
                scratch = New(kScratchSize);
            owned = nullptr;
            buffer = scratch;
        } else {
            buffer = New(size);
            owned = buffer;
        }
        if (Virtual<uint8_t>(self, segment::kReadFile, buffer, offset, size))
            result = Decompress(self, nullptr, buffer, size, out, outSize);
        Delete(owned);
    }
    return result;
}

bool InstallPackedFile1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[72] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZDBSegmentPackedFile_F96D80C, reinterpret_cast<void*>(&RecordSetKey)},
        {addr::cGZDBSegmentPackedFile_F96D824, reinterpret_cast<void*>(&RecordGetAccess)},
        {addr::cGZDBSegmentPackedFile_F96D829, reinterpret_cast<void*>(&RecordSetAccess)},
        {addr::cGZDBSegmentPackedFile_F96D835, reinterpret_cast<void*>(&SharedBufferRelease)},
        {addr::cGZDBSegmentPackedFile_F96D859, reinterpret_cast<void*>(&RecordConstruct)},
        {addr::cGZDBSegmentPackedFile_F96D895, reinterpret_cast<void*>(&KeyEquals)},
        {addr::cGZDBSegmentPackedFile_F96D8BC, reinterpret_cast<void*>(&HeaderConstruct)},
        {addr::cGZDBSegmentPackedFile_F96D8D0, reinterpret_cast<void*>(&FileRecordConstruct)},
        {addr::cGZDBSegmentPackedFile_F96D92C, reinterpret_cast<void*>(&RecordSeekAbsolute)},
        {addr::cGZDBSegmentPackedFile_F96D948, reinterpret_cast<void*>(&FileRecordSeekRelative)},
        {addr::cGZDBSegmentPackedFile_F96D967, reinterpret_cast<void*>(&FileRecordGetVoid)},
        {addr::cGZDBSegmentPackedFile_F96D9B3, reinterpret_cast<void*>(&FileRecordClosed)},
        {addr::cGZDBSegmentPackedFile_F96D9D1, reinterpret_cast<void*>(&MemoryRecordConstructShared)},
        {addr::cGZDBSegmentPackedFile_F96DA30, reinterpret_cast<void*>(&MemoryRecordConstruct)},
        {addr::cGZDBSegmentPackedFile_F96DAA5, reinterpret_cast<void*>(&MemoryRecordDestruct)},
        {addr::cGZDBSegmentPackedFile_F96DAD7, reinterpret_cast<void*>(&MemoryRecordSeekRelative)},
        {addr::cGZDBSegmentPackedFile_F96DAF8, reinterpret_cast<void*>(&MemoryRecordGetVoid)},
        {addr::cGZDBSegmentPackedFile_F96DB35, reinterpret_cast<void*>(&MemoryRecordSetVoid)},
        {addr::cGZDBSegmentPackedFile_F96DB72, reinterpret_cast<void*>(&MemoryRecordClosed)},
        {addr::cGZDBSegmentPackedFile_F96DC2D, reinterpret_cast<void*>(&ViewQueryInterface)},
        {addr::cGZDBSegmentPackedFile_F96DC6E, reinterpret_cast<void*>(&ViewFinalRelease)},
        {addr::cGZDBSegmentPackedFile_F96DC93, reinterpret_cast<void*>(&ViewSetRecord)},
        {addr::cGZDBSegmentPackedFile_F96DCBC, reinterpret_cast<void*>(&ViewClose)},
        {addr::cGZDBSegmentPackedFile_F96DCE4, reinterpret_cast<void*>(&ViewPosition)},
        {addr::cGZDBSegmentPackedFile_F96DCF9, reinterpret_cast<void*>(&ViewLength)},
        {addr::cGZDBSegmentPackedFile_F96DD0C, reinterpret_cast<void*>(&ViewSetLength)},
        {addr::cGZDBSegmentPackedFile_F96DD22, reinterpret_cast<void*>(&ViewSeekToBegin)},
        {addr::cGZDBSegmentPackedFile_F96DD40, reinterpret_cast<void*>(&ViewSeekToEnd)},
        {addr::cGZDBSegmentPackedFile_F96DD75, reinterpret_cast<void*>(&ViewSeekRelative)},
        {addr::cGZDBSegmentPackedFile_F96DDA5, reinterpret_cast<void*>(&ViewSeekToPosition)},
        {addr::cGZDBSegmentPackedFile_F96DDCA, reinterpret_cast<void*>(&ViewSeek)},
        {addr::cGZDBSegmentPackedFile_F96DE22, reinterpret_cast<void*>(&ViewRead)},
        {addr::cGZDBSegmentPackedFile_F96DE35, reinterpret_cast<void*>(&ViewReadWithCount)},
        {addr::cGZDBSegmentPackedFile_F96DE7E, reinterpret_cast<void*>(&ViewWrite)},
        {addr::cGZDBSegmentPackedFile_F96DE94, reinterpret_cast<void*>(&ViewWriteWithCount)},
        {addr::cGZDBSegmentPackedFile_F96DEC1, reinterpret_cast<void*>(&ViewGetOpenModes)},
        {addr::cGZDBSegmentPackedFile_QueryInterface, reinterpret_cast<void*>(&SegmentQueryInterface)},
        {addr::cGZDBSegmentPackedFile_F96DF3C, reinterpret_cast<void*>(&SegmentFinalRelease)},
        {addr::cGZDBSegmentPackedFile_F96DF60, reinterpret_cast<void*>(&SegmentInit)},
        {addr::cGZDBSegmentPackedFile_Shutdown, reinterpret_cast<void*>(&SegmentShutdown)},
        {addr::cGZDBSegmentPackedFile_IsOpen, reinterpret_cast<void*>(&SegmentIsOpen)},
        {addr::cGZDBSegmentPackedFile_F96DFC9, reinterpret_cast<void*>(&SegmentSetId)},
        {addr::cGZDBSegmentPackedFile_F96DFD7, reinterpret_cast<void*>(&SegmentSetBaseOffset)},
        {addr::cGZDBSegmentPackedFile_F96DFF0, reinterpret_cast<void*>(&SegmentSetPath)},
        {addr::cGZDBSegmentPackedFile_F96E02C, reinterpret_cast<void*>(&SegmentGetHeaderUserVersion)},
        {addr::cGZDBSegmentPackedFile_F96E063, reinterpret_cast<void*>(&SegmentGetUserVersion)},
        {addr::cGZDBSegmentPackedFile_F96E09A, reinterpret_cast<void*>(&SegmentSetUserVersion)},
        {addr::cGZDBSegmentPackedFile_F96E0CD, reinterpret_cast<void*>(&SegmentGetDates)},
        {addr::cGZDBSegmentPackedFile_F96E112, reinterpret_cast<void*>(&SegmentSetBuffering)},
        {addr::cGZDBSegmentPackedFile_F96E198, reinterpret_cast<void*>(&SegmentSeekFile)},
        {addr::cGZDBSegmentPackedFile_F96E1E5, reinterpret_cast<void*>(&SegmentReadFile)},
        {addr::cGZDBSegmentPackedFile_F96E246, reinterpret_cast<void*>(&SegmentReadFileWithCount)},
        {addr::cGZDBSegmentPackedFile_F96E30D, reinterpret_cast<void*>(&SegmentIsOpenLocked)},
        {addr::cGZDBSegmentPackedFile_F96E363, reinterpret_cast<void*>(&SegmentRecordClosed)},
        {addr::cGZDBSegmentPackedFile_F96E407, reinterpret_cast<void*>(&SegmentValidateHeader)},
        {addr::cGZDBSegmentPackedFile_F96E49D, reinterpret_cast<void*>(&SegmentValidateHoles)},
        {addr::cGZDBSegmentPackedFile_F96E617, reinterpret_cast<void*>(&Decompress)},
        {addr::cGZDBSegmentPackedFile_F96E6F4, reinterpret_cast<void*>(&SegmentReadCompressed)},
        {addr::cGZDBSegmentPackedFile_F96E78B, reinterpret_cast<void*>(&IndexEntryCopy)},
        {addr::cGZDBSegmentPackedFile_F96E893, reinterpret_cast<void*>(&RecordDelete)},
        {addr::cGZDBSegmentPackedFile_F96E8AF, reinterpret_cast<void*>(&FileRecordDelete)},
        {addr::cGZDBSegmentPackedFile_F96E8D5, reinterpret_cast<void*>(&FileRecordDestruct)},
        {addr::cGZDBSegmentPackedFile_F96E8F5, reinterpret_cast<void*>(&MemoryRecordDelete)},
        {addr::cGZDBSegmentPackedFile_F96E93A, reinterpret_cast<void*>(&ViewDestruct)},
        {addr::cGZDBSegmentPackedFile_F96E96B, reinterpret_cast<void*>(&ViewRelease)},
        {addr::cGZDBSegmentPackedFile_GetPath, reinterpret_cast<void*>(&SegmentGetPath)},
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

const bool g_packedFile1Registered = registry::Register({
    "resources.packed_file.part1",
    "resources",
    registry::Kind::Detour,
    registry::Phase::PreAppInit,
    addr::cGZDBSegmentPackedFile_F96D80C,
    "Our packed file, part 1: read records, the raw file view, the segment's small methods, decompression",
    false,
    InstallPackedFile1,
});

} // namespace
} // namespace opensc4::packed_file
